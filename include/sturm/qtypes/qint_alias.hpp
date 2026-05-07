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
// The load-bearing piece is the **implicit** `operator size_t() const
// noexcept`. That is the *only* implicit-measurement site introduced by
// the QRAM-subscript epic. Per PRD §5, the alias model relies on a
// post-transpile compile-time safety net: pre-transpile, this implicit
// conversion lets `a[qint_idx]` parse; post-transpile, the C1 matcher
// has rewritten the expression to `QRAM_read(a, i, b)` and the emitted
// file references only `qint_t<W>` (whose `operator int64_t()` is
// `explicit`, so any missed site becomes a compile error rather than
// a silent runtime measurement).
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

#include <atomic>
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

// ── qint_alias_detail ─────────────────────────────────────────────────────
// Thread-local measurement counter used as the observable side-effect of
// the frontend `qint`'s implicit `operator size_t()`. Tests pin
// `measurement_count()` going up by one per implicit-conversion site, so
// the post-transpile end-to-end test (G1, sturm-u9ge.17) can use the
// *same* counter to assert it stays at zero — i.e. the matcher erased
// every conversion site.
//
// Implementation choices:
//   * `inline thread_local` — header-only; one counter per thread, no
//     ODR collisions (matches `detail::g_sink` in counter_sink.hpp).
//   * Plain `std::size_t`, not atomic — single-thread observability is
//     the contract; cross-thread aggregation is out of scope.
//
// The counter is intentionally `qint_alias_detail::measurement_count`
// rather than living on a sink object: the implicit conversion happens
// in arbitrary integral contexts (subscript, comparison, range-for
// bound, …) where there is no natural sink hookpoint, and we want the
// observability to work in tests that do not install a custom sink.
namespace qint_alias_detail {

inline thread_local std::size_t g_measurement_count = 0;

inline std::size_t measurement_count() noexcept {
    return g_measurement_count;
}

inline void reset_measurement_count() noexcept {
    g_measurement_count = 0;
}

inline void bump_measurement_count() noexcept {
    ++g_measurement_count;
}

} // namespace qint_alias_detail

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
    // body is unreachable on the rewrite path. If the matcher ever
    // misses the site, the body bumps the same measurement counter as
    // `operator size_t()` so the regression is observable to the G1
    // e2e check (`measurement_count() == 0` post-transpile).
    //
    // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
    template <std::size_t W>
    qint(const ::sturm::qint_t<W>& src) noexcept;

    // ── Implicit operator size_t (the load-bearing piece) ────────────────
    // PRD §4.1: "Implicit `operator size_t() const noexcept;`. This is
    // what makes `a[qint_idx]` parse uniformly across `std::array<qint,
    // N>`, C-style arrays (`qint a[10]`), and pointers (`qint *a`)."
    //
    // Body: invoke the (notional) measurement and return the measured
    // value cast to size_t. In the alias-class model (PRD §5):
    //   * Pre-transpile, this body is reached and *does* measure — that
    //     is the silent-measurement footgun §10.1 accepts.
    //   * Post-transpile, the C1 matcher has rewritten every matched
    //     subscript site, and this body is unreachable. The G1 e2e test
    //     pins that `measurement_count()` stays at 0 across a fully-
    //     transpiled run.
    //
    // The body is defined out-of-line below `class qint` so the
    // class definition stays compact and readable.
    // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
    operator std::size_t() const noexcept;

    // ── Member ops added by sturm-65rs.2 (Beat A1 / sturm-qac.2) ─────────
    // PRD §4.1 member-ops bullet, plan §4 / A1. Three member ops mirror
    // shapes carried by `qint_t<W>` so user code that compiles against
    // either spelling stays compiling against the alias.

    // operator=(int64_t v) — classical-int copy-assign on an existing
    // `qint`. P4a: classical-to-quantum is FREE — no measurement-counter
    // bump. (Counterpart: qint_core.hpp:181, where the backend bumps no
    // counter either; the qubit-pool releases it does are bookkeeping
    // not measurement.)
    qint& operator=(int64_t v) noexcept {
        value_ = v;
        return *this;
    }

    // operator[](size_t k) const — bit read. Wave-3 G7: returns
    // `sturm::qbool` (was classical `bool` pre-W3) so the alias's
    // `operator[]` return type matches `qint_t<W>::operator[]`. The
    // body lives out-of-line in `qint_alias_ops.hpp` (PRD §10.3.4) so
    // this header avoids pulling `sturm/qtypes/qbool.hpp` (and its
    // transitive `qint_core.hpp`) into every TU that only needs the
    // bare alias class. A forward declaration of `sturm::qbool` is
    // already in scope (above the `frontend::` namespace), which is
    // sufficient for the by-value return-type spelling.
    qbool operator[](std::size_t k) const noexcept;

    // explicit operator int64_t() — converting cast. `explicit` so any
    // *implicit* int64_t use is a compile error; the load-bearing
    // implicit conversion site on the alias is `operator size_t()`
    // alone. Each invocation bumps the counter +1 (backend qint_t<W>'s
    // counterpart is at qint_core.hpp:97; the alias bumps the same
    // observability counter the rest of the alias uses).
    explicit operator int64_t() const noexcept {
        qint_alias_detail::bump_measurement_count();
        return value_;
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
    // Bodies bump the per-thread measurement counter and no-op the
    // classical `value_`: phase rotations have no classical mirror
    // (rotating a classical state is a no-op). The bump preserves the
    // observability contract every other A2 stub uses — counter > 0
    // post-transpile means the C1 matcher missed a substitution site.
    struct PhiProxyStub {
        qint& parent;
        void operator+=(double /*delta*/) noexcept {
            qint_alias_detail::bump_measurement_count();
            // No classical mirror: phase is intrinsically quantum.
        }
        void operator-=(double delta) noexcept { operator+=(-delta); }
    };
    struct ThetaProxyStub {
        qint& parent;
        void operator+=(double /*delta*/) noexcept {
            qint_alias_detail::bump_measurement_count();
            // No classical mirror: rotation is intrinsically quantum.
        }
        void operator-=(double delta) noexcept { operator+=(-delta); }
    };
    [[nodiscard]] PhiProxyStub   phi()   noexcept { return PhiProxyStub{*this}; }
    [[nodiscard]] ThetaProxyStub theta() noexcept { return ThetaProxyStub{*this}; }

    // ── Test / introspection accessor ────────────────────────────────────
    // Direct access to the underlying classical value. NOT a public API
    // for user code (use the implicit conversion); exposed because the
    // test suite needs to verify round-trips without going through the
    // measurement counter every time.
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
// Defined `inline` so the header stays self-contained. The body is the
// "measurement-then-classical" stub — bumps the per-thread counter (so
// tests / G1 can observe it ran) and returns the classical value cast
// to `size_t`.
//
// TODO(backend): when the real quantum measurement path lands, replace
// the bump-and-return body with a call into the active backend's
// measurement op (parallel to `qint_t<W>::operator int64_t()`'s
// TODO(backend) at qint_core.hpp:96). The counter will remain — its
// purpose is observability for the matcher's coverage assertion.
inline qint::operator std::size_t() const noexcept {
    qint_alias_detail::bump_measurement_count();
    return static_cast<std::size_t>(value_);
}

// ── Out-of-line converting constructor from backend qint_t<W> ────────────
// Body never accesses `src` (we don't pull qint_core.hpp here — keeps the
// frontend header decoupled from the backend layout). Bumps the
// measurement counter on the same rationale as `operator size_t()`: a
// missed C1 rewrite shows up immediately in the G1 e2e assertion.
template <std::size_t W>
inline qint::qint(const ::sturm::qint_t<W>& /*src*/) noexcept : value_(0) {
    qint_alias_detail::bump_measurement_count();
}

// ── Sanity: the implicit conversion really IS implicit ───────────────────
// We do NOT static_assert this here — the compiler must be free to
// instantiate this header without the test's negative-SFINAE machinery
// loaded. The positive assertion lives in
// `tests/qtypes/test_qint_alias.cpp`.

} // namespace frontend
} // namespace sturm
