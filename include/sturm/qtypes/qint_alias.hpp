#pragma once
// qint_alias.hpp — Frontend `qint` alias class (sturm-u9ge.5 / Beat A1).
//
// This header introduces the **non-templated** frontend `qint` class
// described in `docs/prd_qram_subscript.md` §4.1. It is the user-facing
// type that makes the source spelling
//
//     qint b = a[i];   // i is quantum
//
// parse uniformly across `std::array<qint, N>`, C-arrays (`qint a[N]`),
// and pointers (`qint* a`) — the three container shapes the C1 matcher
// recognises in PRD §7.
//
// Wave 3 contract (PRD §10, epic `sturm-v0db`): the alias is a **pure
// type-stub class** with a **mandatory** transpiler. Every operator
// body is a trivial expression that produces a default-constructed
// return value (`return qbool();` / `return qint{};` / `return 0;` /
// `return *this;` / `{}`); no body reads or writes `value_`, calls
// into any helper, or counts anything. The Wave-1 "lossy-correct
// runtime fallback" framing — where the implicit `operator size_t()`
// was the load-bearing piece — is obsolete: under Wave 3 the
// transpiler is mandatory (host-clang invariant `sturm-yial`), the
// alias never executes at runtime, and the bodies need only
// type-check pre-transpile. The Wave-2 G6 `test_sturm_gen_clean` gate
// is THE coverage contract — it asserts the post-transpile output
// contains zero `sturm::frontend::qint` spellings, which is strictly
// stronger than any pre-transpile counter check could be.
//
// `is_convertible_v<qint, size_t>` is preserved as a parse-time
// invariant (the implicit `operator size_t() const noexcept` declaration
// stays — its body is `return 0;`); this keeps `a[qint_idx]` parsing
// uniformly across `std::array<qint, N>`, C-arrays (`qint a[N]`), and
// pointers (`qint* a`) — the three container shapes the C1 matcher
// recognises in PRD §7. The matcher then rewrites the expression to
// `QRAM_read(a, i, b)` and the emitted file references only
// `qint_t<W>` post-transpile.
//
// Beat A1 deliberately keeps the surface tiny:
//   * default ctor
//   * implicit `qint(int64_t)` (P4a — classical-to-quantum is free)
//   * copy / move = default
//   * implicit `operator size_t() const noexcept` (PRD §4.1)
//
// Arithmetic / compare / bitwise / compound-assign stubs live in
// `qint_alias_ops.hpp` (Beat A2 = sturm-u9ge.10). This file deliberately
// declares none of them — keeping the A1 surface auditable.
//
// Namespace: `sturm::frontend::qint`. The PRD's long-term target is for
// the user-level spelling `sturm::qint` to resolve to *this* class; that
// migration is a follow-up step (the existing `using qint = qint_t<64>;`
// in `qint_fwd.hpp` is referenced by 50+ tests/fixtures and cannot be
// repointed in a single beat). For now, A1's tests alias locally:
//     using qint = sturm::frontend::qint;
// which is the same shape every transpiler fixture uses today.
//
// LoC budget: <= 300 (plan §1, §4 / A1).

#include "sturm/qtypes/qint_fwd.hpp"   // sturm::qint_t<W> for the negative
                                       // SFINAE in the test (consumer-side).

#include <cstddef>
#include <cstdint>

namespace sturm {

// ── Forward declaration of qbool (Wave-3 G7 + G10) ────────────────────────
// The frontend `qint::operator[](size_t) const` returns `sturm::qbool`. The
// FULL definition lives in `sturm/qtypes/qbool.hpp`, which is intentionally
// NOT included here (PRD §10.3.4: keep the bare class header free of
// backend includes — the include is added in `qint_alias_ops.hpp` along
// with the out-of-line `operator[]` definition). A forward declaration is
// sufficient for the in-class declaration because returning `qbool` by
// value in a function declaration only requires the type to be declared,
// not complete.
class qbool;

namespace frontend {

// ── Wave 3 / W3.4 / G9 (sturm-v0db.5, PRD §10.3.3) ───────────────────────
// The legacy frontend detail namespace (thread-local measurement
// counter + bump/reset/get accessors) has been deleted. Under Wave 3
// the alias's operator bodies are pure type-stubs (W3.3 / G8); none
// of them count anything. Pre-transpile observability moves to the
// Wave-2 G6 `test_sturm_gen_clean` gate (a strictly stronger
// contract: the rewritten source must contain zero alias type-
// spellings, regardless of any runtime counter). Future
// re-introduction of any deleted symbol is gated by W3.6's tree-grep
// audit (`tests/qtypes/test_qint_alias_no_counter_infra`) and the
// parallel forbidden-substring family in
// `transpiler/tests/test_sturm_gen_clean_scan.hpp`.

// ── class qint ───────────────────────────────────────────────────────────
// Frontend, non-templated, intentionally minimal. The implicit
// `operator size_t() const noexcept` is the only operator declared at A1.
//
// Storage: a single `int64_t value_` representing the classical part of
// the would-be quantum integer. Beats B1 / C1 do not need any quantum
// data on this class — by the time the matcher fires, the substitution
// `qint -> qint_t<W>` has already happened, so the frontend type's
// internal storage layout is private to the pre-transpile compile.
//
// We deliberately do NOT inherit from / contain a `qint_t<W>`: that
// would force a width choice on the frontend, defeating §4 step 1's
// "non-templated" requirement and bleeding backend semantics into the
// frontend.
class qint {
public:
    // ── Default constructor ──────────────────────────────────────────────
    // P4a: a fully-classical default-constructed qint is free —
    // emits nothing to the sink and after inlining costs the same as
    // `int64_t v = 0;`.
    qint() noexcept = default;

    // ── Implicit classical-int constructor ───────────────────────────────
    // P4a: classical-to-quantum conversion is implicit and free. The
    // ctor is intentionally NOT marked `explicit` so user code like
    // `qint a = 42;` parses without ceremony.
    // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
    qint(int64_t v) noexcept : value_(v) {}

    // ── Copy / move = default ────────────────────────────────────────────
    // PRD §4.1 step 1: "copy/move = default". Triviality matters — the
    // C++ inliner relies on it for the P4a "free at runtime" guarantee.
    qint(const qint&)            noexcept = default;
    qint(qint&&)                 noexcept = default;
    qint& operator=(const qint&) noexcept = default;
    qint& operator=(qint&&)      noexcept = default;
    ~qint()                      noexcept = default;

    // ── Implicit converting constructor from backend `qint_t<W>` ─────────
    // Lets the user-facing source line `qint b = a[i];` PARSE when `a`
    // is `std::array<qint_t<W>, N>` / `qint_t<W>[N]` / `qint_t<W>*` —
    // a[i] returns `qint_t<W>&`, this ctor accepts it.
    //
    // Post-transpile the C1 matcher REPLACES the VarDecl initializer
    // with the explicit `sturm::qint_t<W> b; ::sturm::QRAM_read(a, i, b);`
    // pair (PRD §8 / archive/prd_qram_subscript.md §9 row 3), so this
    // body is unreachable on the rewrite path. Wave 3 (sturm-v0db.4 /
    // W3.3, PRD §10.3.2 / G8): pure type-stub `{}`. The Wave-2 G6
    // `test_sturm_gen_clean` gate enforces the post-rewrite invariant
    // (zero alias type-spellings in the emitted source) — strictly
    // stronger than the deprecated W2-era runtime counter check.
    //
    // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
    template <std::size_t W>
    qint(const ::sturm::qint_t<W>& src) noexcept;

    // ── Implicit operator size_t — parse-time invariant ──────────────────
    // PRD §4.1 / Wave 3 §10.3.6: the implicit `operator size_t() const`
    // is preserved as a **parse-time invariant** (not a load-bearing
    // runtime measurement site). Its sole role under Wave 3 is to keep
    // `is_convertible_v<qint, size_t>` true, which is what makes
    // `a[qint_idx]` parse uniformly across `std::array<qint, N>`,
    // C-style arrays (`qint a[10]`), and pointers (`qint *a`) — the
    // three container shapes the C1 matcher recognises in PRD §7.
    //
    // Wave 3 (sturm-v0db.4 / W3.3, PRD §10.3.2 / G8): the body is a
    // pure type-stub `return 0;`. The transpiler is mandatory under
    // Wave 3 (host-clang invariant sturm-yial); the C1 matcher has
    // rewritten every matched subscript site, so the body is
    // unreachable on the rewrite path. The Wave-2 G6
    // `test_sturm_gen_clean` gate (THE coverage contract under Wave 3)
    // pins that the rewritten source has zero alias type-spellings.
    //
    // The body is defined out-of-line below `class qint` so the
    // class definition stays compact and readable.
    // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
    operator std::size_t() const noexcept;

    // ── Member ops added by sturm-65rs.2 (Beat A1 / sturm-qac.2) ─────────
    // PRD §4.1 member-ops bullet, plan §4 / A1. Three member ops mirror
    // shapes carried by `qint_t<W>` so user code that compiles against
    // either spelling stays compiling against the alias.
    //
    // Wave 3 (sturm-v0db.4 / W3.3, PRD §10.3.2 / G8): every body is a
    // pure type-stub. The transpiler is mandatory (sturm-yial host-clang
    // invariant); the C1 matcher rewrites the alias usage so these
    // bodies are unreachable on the rewrite path. They exist purely so
    // the host C++ compiler can typecheck the source file pre-transpile.

    // operator=(int64_t v) — pure type-stub. Body returns `*this`
    // unchanged; does NOT write `value_` (G8 — the only `value_` writes
    // in the alias are the class default `= 0` and the int64_t ctor's
    // mem-init).
    qint& operator=(int64_t /*v*/) noexcept {
        return *this;
    }

    // operator[](size_t k) const — bit read. Wave-3 G7: returns
    // `sturm::qbool`. The body lives out-of-line in `qint_alias_ops.hpp`
    // (PRD §10.3.4) so this header avoids pulling `sturm/qtypes/qbool.hpp`
    // (and its transitive `qint_core.hpp`) into every TU that only needs
    // the bare alias class. A forward declaration of `sturm::qbool` is
    // already in scope (above the `frontend::` namespace), which is
    // sufficient for the by-value return-type spelling. W3.3 strips the
    // out-of-line body to `return qbool();`.
    qbool operator[](std::size_t k) const noexcept;

    // explicit operator int64_t() — pure type-stub. `explicit` so any
    // *implicit* int64_t use is a compile error; the parse-time
    // implicit conversion site on the alias is `operator size_t()`
    // alone (preserved as the Wave-3 invariant — see header banner).
    // Body returns `0`; does NOT read `value_` (G8).
    explicit operator int64_t() const noexcept {
        return 0;
    }

    // ── Phase / rotation proxy stubs (sturm-vm38) ────────────────────────
    // Mirror `qint_t<W>::PhiProxy` / `::ThetaProxy` from qint_core.hpp:241
    // / :308. PRD §4.1 step 2: the alias mirrors `qint_t<W>`'s public
    // surface; phi()/theta() were missed by the original A2 SFINAE
    // drift-gate (which enumerated operator overloads only, not named
    // member methods), so user code like `qint i = 2; i.phi() += 3;`
    // failed to parse pre-transpile even though the C1 matcher would
    // have substituted `frontend::qint -> sturm::qint_t<W>` and routed
    // the call to the real backend proxy post-transpile.
    //
    // Wave 3 (sturm-v0db.4 / W3.3, PRD §10.3.2 / G8): bodies are pure
    // type-stubs (`{}`). No counter bumps, no `value_` access. The C1
    // matcher rewrites `frontend::qint` to `sturm::qint_t<W>` so these
    // bodies are unreachable on the rewrite path.
    struct PhiProxyStub {
        qint& parent;
        void operator+=(double /*delta*/) noexcept {}
        void operator-=(double /*delta*/) noexcept {}
    };
    struct ThetaProxyStub {
        qint& parent;
        void operator+=(double /*delta*/) noexcept {}
        void operator-=(double /*delta*/) noexcept {}
    };
    [[nodiscard]] PhiProxyStub   phi()   noexcept { return PhiProxyStub{*this}; }
    [[nodiscard]] ThetaProxyStub theta() noexcept { return ThetaProxyStub{*this}; }

    // ── Test / introspection accessor ────────────────────────────────────
    // Direct access to the underlying classical value. NOT a public API
    // for user code (use the implicit conversion); exposed because the
    // test suite needs to verify round-trips without going through the
    // implicit conversion path every time.
    [[nodiscard]] int64_t classical_value() const noexcept { return value_; }

private:
    // Classical part of the would-be quantum integer. Quantum data
    // (per-bit qubit indices, super_mask) does not live here — the
    // frontend `qint` is a pre-transpile placeholder; the matcher
    // substitutes `qint_t<W>` into the emitted code, and that backend
    // type owns the quantum state.
    int64_t value_ = 0;
};

// ── Out-of-line operator size_t ──────────────────────────────────────────
// Wave 3 (sturm-v0db.4 / W3.3, PRD §10.3.2 / G8): pure type-stub. The
// body returns `0` and never reads `value_`. The declaration's sole
// purpose is to keep `is_convertible_v<qint, size_t>` true (parse-time
// invariant that makes `a[qint_idx]` parse — see in-class comment).
// Under Wave 3 the transpiler is mandatory (host-clang invariant
// sturm-yial); the C1 matcher rewrites every matched subscript site,
// so this body is unreachable on the rewrite path. The Wave-2 G6
// `test_sturm_gen_clean` gate is THE coverage contract — it verifies
// the post-transpile output contains zero alias type-spellings.
inline qint::operator std::size_t() const noexcept {
    return 0;
}

// ── Out-of-line converting constructor from backend qint_t<W> ────────────
// Wave 3 (sturm-v0db.4 / W3.3, PRD §10.3.2 / G8): pure type-stub `{}`.
// No member-init list (note: the class-default `int64_t value_ = 0;`
// still applies, so `value_` is zero-initialised — the body itself
// neither reads nor writes any field). Body never accesses `src` (we
// do not pull qint_core.hpp here — keeps the frontend header decoupled
// from the backend layout).
template <std::size_t W>
inline qint::qint(const ::sturm::qint_t<W>& /*src*/) noexcept {}

// ── Sanity: the implicit conversion really IS implicit ───────────────────
// We do NOT static_assert this here — the compiler must be free to
// instantiate this header without the test's negative-SFINAE machinery
// loaded. The positive assertion lives in
// `tests/qtypes/test_qint_alias.cpp`.

} // namespace frontend
} // namespace sturm
