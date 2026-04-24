#pragma once
// when_scope_garbage.hpp — WHEN scope exit consumer for the garbage_registry.
// sturm-njul (P3, materialised as a diagnostic-only pass).
//
// Purpose
// -------
// The garbage_registry (sturm-h5it.5, sturm-pqs0) makes the leaked W-qubit
// registers produced by controlled lossy compound assignments discoverable.
// This header adds the missing scope-exit consumer: a per-WhenGuard helper
// that records the registry baseline at scope entry and, at scope exit,
// consumes the records registered during the scope body.
//
// Honest scope
// ------------
// General gate-level uncomputation of these leaks is **impossible in place**:
//
//   - AND_ASSIGN / OR_ASSIGN: the leaked W-bit register holds a CCX-derived
//     function of the **pre-op** A and B. After the CSWAP tail has fired, A
//     has been swapped with the result register, so the pre-op A values
//     needed to reverse the forward CCX chain no longer exist.
//   - MUL_ASSIGN / DIV_ASSIGN / MOD_ASSIGN: the lossy forward op destroyed
//     the classical input we'd need to reverse it.
//   - MUL_UPPER_W / DIV_REMAINDER: entangled with the current A/B; reversing
//     would require re-running the forward op against the current (post-op)
//     state, which changes A.
//
// Bennett-style "save a copy of A before the lossy op, uncompute after"
// would restore reversibility at the cost of a second W-qubit register per
// lossy call — which defeats the point of the in-place CSWAP fix that the
// sturm-h5it epic shipped. That broader direction is tracked separately
// (see sturm-pqs0 and the new Bennett-style follow-up issue).
//
// What this pass does
// -------------------
// 1. Baseline tracking. WhenScopeGarbage captures
//    `garbage_registry::snapshot().size()` at WHEN scope entry. On scope
//    exit (destructor) it walks [baseline .. end) — the records registered
//    by lossy ops inside the scope body — and consumes them.
//
// 2. Consumption. The records in [baseline .. end) are **popped** from the
//    thread-local registry (by shrinking the vector back to the baseline).
//    This is deliberate: without this pass, records would accumulate
//    monotonically across scopes, and the registry vector would grow
//    unboundedly over a program run. Popping them back to the baseline is
//    the minimum the scope-exit pass can honestly do, and it keeps the
//    registry bounded by the depth of the WHEN nest, not the call count.
//
// 3. Diagnostic. When compiled with STURM_GARBAGE_REPORT defined (or when
//    the env var STURM_GARBAGE_REPORT is non-empty at runtime), the pass
//    emits a one-line report per WHEN scope exit to stderr:
//       [STURM garbage] WHEN scope exit: N records leaked (tags: ...)
//    The report lists the per-tag counts of the records that were consumed.
//    No gates are emitted — the pre-op state is gone, so honest in-place
//    uncomputation is impossible.
//
// 4. Nesting. Each WhenGuard owns one WhenScopeGarbage. Inner scopes
//    consume only the records added during the inner body; the outer scope
//    consumes whatever the outer body (minus already-consumed inner spans)
//    registered. Records registered *before* any WHEN scope — e.g. by an
//    uncontrolled `*=` via MUL_UPPER_W — stay below every baseline and
//    are never consumed by this pass (they are an op-level concern, not
//    a WHEN-scope concern).
//
// API
// ---
//   namespace sturm::detail {
//     struct WhenScopeGarbage {
//       WhenScopeGarbage() noexcept;   // captures baseline
//       ~WhenScopeGarbage() noexcept;  // consumes [baseline .. end)
//       // Non-copyable, non-movable.
//     };
//     // Free-function entry point (used by WhenGuard) — equivalent to the
//     // destructor, useful when the caller wants to consume manually.
//     void consume_scope_garbage(std::size_t baseline) noexcept;
//
//     // Query whether the diagnostic report is enabled (compile-time macro
//     // or STURM_GARBAGE_REPORT env var). Exposed for tests.
//     bool garbage_report_enabled() noexcept;
//   }

#include "sturm/control/garbage_registry.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace sturm::detail {

// ── consume_enabled TLS (test hook) ─────────────────────────────────────────
// Per-thread flag that gates whether WhenScopeGarbage actually consumes
// (pops) records at scope exit. Default: true — production code wants the
// registry to stay bounded by the depth of the WHEN nest.
//
// Tests that observe the post-WHEN registry state (e.g. sturm-h5it /
// sturm-pqs0 discoverability tests in tests/backend/) temporarily disable
// the consumer via a ScopedGarbageConsumeGuard RAII helper or the
// free-function setter below. When disabled, WhenScopeGarbage still
// records the baseline (so re-enabling mid-run cannot corrupt the
// registry) but its destructor is a no-op.
namespace _tls {
    inline thread_local bool garbage_consume_enabled = true;
} // namespace _tls

inline bool get_garbage_consume_enabled() noexcept {
    return _tls::garbage_consume_enabled;
}

inline void set_garbage_consume_enabled(bool v) noexcept {
    _tls::garbage_consume_enabled = v;
}

// ── ScopedGarbageConsumeGuard ───────────────────────────────────────────────
// RAII helper to temporarily disable (or enable) the scope-exit consumer.
// Test-only — product code should leave the default (true) alone.
struct ScopedGarbageConsumeGuard {
    bool prev_;
    explicit ScopedGarbageConsumeGuard(bool new_state) noexcept
        : prev_(get_garbage_consume_enabled()) {
        set_garbage_consume_enabled(new_state);
    }
    ~ScopedGarbageConsumeGuard() noexcept {
        set_garbage_consume_enabled(prev_);
    }
    ScopedGarbageConsumeGuard(const ScopedGarbageConsumeGuard&)            = delete;
    ScopedGarbageConsumeGuard& operator=(const ScopedGarbageConsumeGuard&) = delete;
    ScopedGarbageConsumeGuard(ScopedGarbageConsumeGuard&&)                 = delete;
    ScopedGarbageConsumeGuard& operator=(ScopedGarbageConsumeGuard&&)      = delete;
};

// ── garbage_report_enabled ──────────────────────────────────────────────────
// Returns true when the one-line diagnostic should be emitted at scope exit.
// Sources (checked lazily on first call, cached thereafter):
//   - #define STURM_GARBAGE_REPORT (compile-time force-on)
//   - getenv("STURM_GARBAGE_REPORT") returning a non-empty string.
// Any non-empty env value enables the report. Absent / empty disables it.
inline bool garbage_report_enabled() noexcept {
#ifdef STURM_GARBAGE_REPORT
    return true;
#else
    // Cache per-thread: reading env on every scope exit is measurable under
    // tight loops, and the env is effectively immutable for the run.
    static thread_local int cached = -1;
    if (cached < 0) {
        const char* v = std::getenv("STURM_GARBAGE_REPORT");
        cached = (v != nullptr && v[0] != '\0') ? 1 : 0;
    }
    return cached != 0;
#endif
}

// ── tag_name ────────────────────────────────────────────────────────────────
// Short human-readable name for each source_op_tag, used by the diagnostic.
inline const char* tag_name(garbage_registry::source_op_tag t) noexcept {
    using T = garbage_registry::source_op_tag;
    switch (t) {
        case T::AND_ASSIGN:    return "AND";
        case T::OR_ASSIGN:     return "OR";
        case T::MUL_ASSIGN:    return "MUL";
        case T::DIV_ASSIGN:    return "DIV";
        case T::MOD_ASSIGN:    return "MOD";
        case T::MUL_UPPER_W:   return "MUL_UPPER_W";
        case T::DIV_REMAINDER: return "DIV_REMAINDER";
    }
    return "?";
}

// ── consume_scope_garbage ───────────────────────────────────────────────────
// Walk the records in [baseline .. end) of the thread-local registry, emit
// the diagnostic report if enabled, and pop them. Safe to call with
// baseline >= snapshot().size() (no-op).
inline void consume_scope_garbage(std::size_t baseline) noexcept {
    // Test-hook: when disabled, preserve the registry contents so existing
    // discoverability tests (sturm-h5it.*, sturm-pqs0) can observe the
    // post-scope record count. The baseline is still captured (it just
    // isn't acted on), so re-enabling mid-program is safe.
    if (!get_garbage_consume_enabled()) {
        return;
    }
    auto& records = garbage_registry::_tls::records;
    if (baseline >= records.size()) {
        return; // Scope registered nothing lossy: nothing to consume.
    }

    if (garbage_report_enabled()) {
        // Count per tag across [baseline .. end).
        // Seven tags total (see garbage_registry.hpp).
        constexpr std::size_t kNumTags = 7;
        std::array<std::size_t, kNumTags> counts{};
        const std::size_t n = records.size() - baseline;
        for (std::size_t i = baseline; i < records.size(); ++i) {
            auto idx = static_cast<unsigned>(records[i].tag);
            if (idx < kNumTags) {
                ++counts[idx];
            }
        }

        // Emit a single stderr line. Keep format stable — tests match on it.
        std::fprintf(stderr,
                     "[STURM garbage] WHEN scope exit: %zu records leaked "
                     "(tags:",
                     n);
        using T = garbage_registry::source_op_tag;
        const T all_tags[kNumTags] = {
            T::AND_ASSIGN, T::OR_ASSIGN, T::MUL_ASSIGN, T::DIV_ASSIGN,
            T::MOD_ASSIGN, T::MUL_UPPER_W, T::DIV_REMAINDER,
        };
        for (std::size_t i = 0; i < kNumTags; ++i) {
            if (counts[i] > 0) {
                std::fprintf(stderr, " %s=%zu", tag_name(all_tags[i]), counts[i]);
            }
        }
        std::fprintf(stderr, ")\n");
    }

    // Pop the records added during this scope back to the baseline. The
    // pre-op state is gone, so no gates can be emitted to uncompute them;
    // consuming the registry entries is the honest diagnostic-only outcome.
    // TODO(backend): replace with Bennett-style with-copy uncomputation when
    // that becomes feasible — see the follow-up issue filed after sturm-njul.
    records.resize(baseline);
}

// ── WhenScopeGarbage ────────────────────────────────────────────────────────
// RAII helper owned by WhenGuard. Captures the registry baseline at
// construction and consumes the scope's new records at destruction.
//
// Designed to live inside WhenGuard so the consumer fires exactly when the
// WHEN scope ends, regardless of normal exit vs. early return. WhenGuard
// already destroys its members in the correct order (guard first, then
// _when_val_); placing this ahead of the control-TLS restore in the guard's
// destructor would require splitting the guard, so instead we install it as
// a sibling member of WhenGuard and let the compiler sequence the
// destruction.
struct WhenScopeGarbage {
    std::size_t baseline_;

    WhenScopeGarbage() noexcept
        : baseline_(garbage_registry::snapshot().size()) {}

    ~WhenScopeGarbage() noexcept {
        consume_scope_garbage(baseline_);
    }

    // Non-copyable, non-movable — sibling of WhenGuard.
    WhenScopeGarbage(const WhenScopeGarbage&)            = delete;
    WhenScopeGarbage& operator=(const WhenScopeGarbage&) = delete;
    WhenScopeGarbage(WhenScopeGarbage&&)                 = delete;
    WhenScopeGarbage& operator=(WhenScopeGarbage&&)      = delete;

    [[nodiscard]] std::size_t baseline() const noexcept { return baseline_; }
};

} // namespace sturm::detail
