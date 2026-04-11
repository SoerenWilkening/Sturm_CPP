#pragma once
// qint_core.hpp — Core class definition for qint_t<Width>.
// Step 6 Module B, spec §3, Implementation Plan §6.
//
// Contains: data members, all constructors, assignment operators, conversions,
// qubits_vec() helper, operator declarations (bodies in arith/bitwise/compare),
// PhiProxy and ThetaProxy nested types.
//
// Arithmetic/bitwise/compare operator definitions are in separate headers
// included via qint.hpp umbrella. The compound-assign member bodies are defined
// in qint_arith.hpp after the free operator+ etc. are visible.

#include "sturm/qtypes/qint_fwd.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/counter_sink.hpp"  // current_sink()
#include "sturm/control/when_fwd.hpp"   // detail::current_control for proxies

// M21: Backend uncompute wiring.
// These headers pull in execute_gate (via qint_base.hpp/context.hpp), so they
// are guarded behind STURM_BACKEND_ENABLED.  Backend test targets define this
// macro; frontend-only test targets omit it and get the stub uncompute_op field
// replaced by a minimal sentinel below.
#ifdef STURM_BACKEND_ENABLED
#  include "sturm/uncompute/uncompute_op.hpp"  // uncompute_op tagged union (M19)
#  include "sturm/uncompute/qint_base.hpp"     // qint_base + add_const/sub_const
#  include "sturm/core/context.hpp"            // BackendContext, execute_gate (M3)
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sturm {

// Forward declaration — full definition is in qbool.hpp.
// qint_core.hpp may not include qbool.hpp directly: once M5 lands, qbool.hpp
// will include qint_core.hpp (for inheritance), which would create a cycle.
// Conversion bodies live in qint_qbool_conv.hpp (included by qint.hpp after
// both headers are available).
class qbool;

template <std::size_t Width>
class qint_t {
    static_assert(Width >= 1 && Width <= 64,
                  "qint_t Width must be in [1, 64]");

public:
    using value_type = int64_t;
    using mask_type  = uint64_t;

    // ── Data members (public for direct test inspection and dispatch helpers) ─
    int64_t  value      = 0;
    uint64_t super_mask = 0;
    std::array<int, Width> qubits{};

    // ── Uncompute op (M21/Strategy B) ─────────────────────────────────────────
    // Carries the semantic inverse of the operation that produced this object.
    // Set by operators that produce an uncomputable result (e.g. operator+(int)).
    // Cleared (NONE) by default; measurement also clears it.
    // Only present when the backend uncompute wiring is compiled in
    // (STURM_BACKEND_ENABLED).  Frontend-only builds omit this field.
#ifdef STURM_BACKEND_ENABLED
    uncompute_op uncompute_{};
#endif

    // ── Ownership flag (M1: owning_ on qint_t) ────────────────────────────────
    // When true (default), the destructor releases qubit indices back to the pool.
    // When false, this object is a non-owning view — qubits are managed elsewhere.
    // Transferred by move; copies always start with owning_ = true and no qubits.
    bool owning_ = true;

    // ── Default constructor ───────────────────────────────────────────────────
    qint_t() noexcept {
        qubits.fill(-1);
    }

    // ── Implicit int64_t constructor ──────────────────────────────────────────
    // NOLINTNEXTLINE(google-explicit-constructor)
    qint_t(int64_t v) noexcept : value(v), super_mask(0) {
        qubits.fill(-1);
    }

    // ── Construct from qbool (zero-extend bit 0) ──────────────────────────────
    // spec §7: value = b.value ? 1 : 0; mask = b.is_super ? 1 : 0;
    // qubits[0] shares the qbool's qubit index.
    // Body is in qint_qbool_conv.hpp (needs full qbool definition).
    // NOLINTNEXTLINE(google-explicit-constructor)
    qint_t(const qbool& b) noexcept;

    // ── Explicit conversion to qbool (keep bit 0 only) ────────────────────────
    // Body is in qint_qbool_conv.hpp (needs full qbool definition).
    explicit operator qbool() const noexcept;

    // ── Explicit int64_t conversion ───────────────────────────────────────────
    // TODO(backend): perform real quantum measurement and collapse state.
    explicit operator int64_t() const noexcept { return value; }

    // ── Copy constructor ──────────────────────────────────────────────────────
    // Copies value and super_mask but does NOT share qubit indices — the copy
    // starts with all qubits == -1.  Callers that need qubits on the copy must
    // invoke ensure_bit_qubit() or otherwise allocate lazily.  This mirrors the
    // qbool copy-constructor semantics and prevents double-release when
    // dispatch helpers take mutable value-copies of operands.
    // owning_ = true: the copy owns its own fresh (unallocated) qubits.
    qint_t(const qint_t& other) noexcept
        : value(other.value), super_mask(other.super_mask), owning_(true) {
        qubits.fill(-1);
    }

    // ── Move constructor ──────────────────────────────────────────────────────
    // Transfers ownership: destination becomes owning, source becomes non-owning.
    qint_t(qint_t&& other) noexcept
        : value(other.value), super_mask(other.super_mask), qubits(other.qubits),
          owning_(other.owning_)
#ifdef STURM_BACKEND_ENABLED
          , uncompute_(other.uncompute_)
#endif
    {
        other.owning_ = false;   // source no longer owns the qubits
        other.qubits.fill(-1);
        other.super_mask = 0;
#ifdef STURM_BACKEND_ENABLED
        other.uncompute_ = uncompute_op{};  // clear so moved-from won't re-emit
#endif
    }

    // ── as_qint_base ──────────────────────────────────────────────────────────
    // Builds a qint_base view of this register's current state.
    // Used by the destructor to pass to uncompute_op::apply and to
    // add_const / sub_const backend stubs.
    // Only available when STURM_BACKEND_ENABLED is set.
#ifdef STURM_BACKEND_ENABLED
    [[nodiscard]] qint_base as_qint_base() const noexcept {
        qint_base b;
        b.value          = value;
        b.super_mask     = super_mask;
        b.promotion_mask = 0u;
        b.width          = static_cast<uint8_t>(Width < QINT_BASE_MAX_WIDTH
                                                ? Width : QINT_BASE_MAX_WIDTH);
        for (uint8_t i = 0; i < b.width; ++i) {
            b.qubits[i] = (qubits[i] >= 0)
                          ? static_cast<uint32_t>(qubits[i]) : 0u;
        }
        return b;
    }
#endif  // STURM_BACKEND_ENABLED

    // ── Destructor ────────────────────────────────────────────────────────────
    // M21: If there is an active BackendContext and the uncompute_op tag is
    // not NONE, emit the semantic inverse via the context before releasing.
    // This is the RAII Strategy B spine for qint_t<W>.
    // When STURM_BACKEND_ENABLED is not set, falls back to the original
    // qubit-release-only destructor (frontend-only builds).
    ~qint_t() {
#ifdef STURM_BACKEND_ENABLED
        // Step 1: emit semantic inverse if an uncompute op is set (Strategy B).
        if (uncompute_.tag != uncompute_op::kind::NONE) {
            if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
                qint_base view = as_qint_base();
                uncompute_.apply(*ctx, view);
            }
        }
#endif
        // Step 2: release qubit indices back to the global pool.
        // Guard with owning_ so non-owning views don't double-release.
        // TODO(backend): migrate to per-context pool when the full
        //                qubit-lifecycle wiring lands (M-future).
        if (owning_) {
            for (int idx : qubits) {
                if (idx >= 0) {
                    QubitPool::instance().release(idx);
                }
            }
        }
    }

    // ── Copy assignment ───────────────────────────────────────────────────────
    // Releases current qubits (if owning), then copies value/super_mask only.
    // Qubit indices are reset to -1 (no shared ownership; prevents double-release).
    // owning_ = true: the copy owns its own fresh (unallocated) qubits.
    qint_t& operator=(const qint_t& other) {
        if (this == &other) return *this;
        if (owning_) {
            for (int idx : qubits) {
                if (idx >= 0) QubitPool::instance().release(idx);
            }
        }
        value      = other.value;
        super_mask = other.super_mask;
        qubits.fill(-1);
        owning_    = true;  // copy owns its own fresh qubits
        return *this;
    }

    // ── Move assignment ───────────────────────────────────────────────────────
    // Transfers ownership: destination becomes owning, source becomes non-owning.
    qint_t& operator=(qint_t&& other) noexcept {
        if (this == &other) return *this;
        if (owning_) {
            for (int idx : qubits) {
                if (idx >= 0) QubitPool::instance().release(idx);
            }
        }
        value      = other.value;
        super_mask = other.super_mask;
        qubits     = other.qubits;
        owning_    = other.owning_;  // transfer ownership
#ifdef STURM_BACKEND_ENABLED
        uncompute_ = other.uncompute_;
#endif
        other.owning_ = false;       // source no longer owns the qubits
        other.qubits.fill(-1);
        other.super_mask = 0;
#ifdef STURM_BACKEND_ENABLED
        other.uncompute_ = uncompute_op{};  // clear so moved-from won't re-emit
#endif
        return *this;
    }

    // ── int64_t assignment ────────────────────────────────────────────────────
    qint_t& operator=(int64_t v) {
        if (owning_) {
            for (int idx : qubits) {
                if (idx >= 0) QubitPool::instance().release(idx);
            }
        }
        value      = v;
        super_mask = 0;
        qubits.fill(-1);
        owning_    = true;
        return *this;
    }

    // ── qubits_vec ────────────────────────────────────────────────────────────
    [[nodiscard]] std::vector<int> qubits_vec() const {
        std::vector<int> v;
        v.reserve(Width);
        for (auto idx : qubits) {
            if (idx >= 0) v.push_back(idx);
        }
        return v;
    }

    // ── Arithmetic operator declarations ──────────────────────────────────────
    // Bodies defined as non-member templates in qint_arith.hpp
    qint_t& operator+=(const qint_t& b);
    qint_t& operator-=(const qint_t& b);
    qint_t& operator*=(const qint_t& b);
    qint_t& operator/=(const qint_t& b);
    qint_t& operator%=(const qint_t& b);

    // ── Bitwise operator declarations ─────────────────────────────────────────
    // Bodies defined as non-member templates in qint_bitwise.hpp
    qint_t& operator&=(const qint_t& b);
    qint_t& operator|=(const qint_t& b);
    qint_t& operator^=(const qint_t& b);
    qint_t& operator<<=(int n);
    qint_t& operator>>=(int n);

    // ── Compare operator declarations ─────────────────────────────────────────
    // Returns qbool; bodies in qint_compare.hpp
    qbool operator==(const qint_t& b) const;
    qbool operator!=(const qint_t& b) const;
    qbool operator< (const qint_t& b) const;
    qbool operator<=(const qint_t& b) const;
    qbool operator> (const qint_t& b) const;
    qbool operator>=(const qint_t& b) const;

    // ── Bit subscript (returns qbool view) ───────────────────────────────────
    // Defined in qint_compare.hpp
    qbool operator[](std::size_t i) const;

    // ── PhiProxy ──────────────────────────────────────────────────────────────
    struct PhiProxy {
        qint_t& parent;
        void operator+=(double delta) {
            const int ctrl = detail::current_control
                             ? detail::current_control_qubit : -1;
            for (std::size_t i = 0; i < Width; ++i) {
                if (parent.qubits[i] >= 0) {
                    current_sink()->phi_add(parent.qubits[i], delta, ctrl);
                }
            }
        }
        void operator-=(double delta) { operator+=(-delta); }
    };

    // ── ThetaProxy ────────────────────────────────────────────────────────────
    struct ThetaProxy {
        qint_t& parent;
        void operator+=(double delta) {
            const int ctrl = detail::current_control
                             ? detail::current_control_qubit : -1;
            for (std::size_t i = 0; i < Width; ++i) {
                if (parent.qubits[i] >= 0) {
                    current_sink()->theta_add(parent.qubits[i], delta, ctrl);
                }
            }
        }
        void operator-=(double delta) { operator+=(-delta); }
    };

    PhiProxy   phi()   { return PhiProxy{*this}; }
    ThetaProxy theta() { return ThetaProxy{*this}; }

    // ── make_non_owning ───────────────────────────────────────────────────────
    // Creates a non-owning view of externally managed qubits.
    // The returned object's destructor will NOT release the qubit indices.
    // Used by dispatch helpers and qbool's make_non_owning to avoid double-release.
    static qint_t make_non_owning(std::array<int, Width> q, int64_t val,
                                  uint64_t mask) {
        qint_t result;
        result.qubits     = q;
        result.value      = val;
        result.super_mask = mask;
        result.owning_    = false;
        return result;
    }
};

} // namespace sturm
