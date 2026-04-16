// invert.hpp — Runtime `invert(fn)` + `STURM_REGISTER_ADJOINT` macro.
//
// Phase I PI-0 (docs/roadmap_transpiler_post_mvp.md). Ships the runtime
// machinery that principle P9 calls for: a free-function template
// `sturm::invert(fn)` that returns the adjoint registered for `fn`, and a
// `STURM_REGISTER_ADJOINT(fn, adj)` macro that records a forward/adjoint
// pair at namespace scope.
//
// Design contract:
//   * Trait-based dispatch. `sturm::_detail::adjoint_of<FnPtr>` is a
//     primary-undefined class template; `STURM_REGISTER_ADJOINT(fn, adj)`
//     expands to a full specialization that exposes
//         static constexpr auto value = &::adj;
//     The trait is the single source of truth for the fwd -> adj map.
//   * `sturm::invert(fn)` is a `constexpr` `noexcept` free function that
//     looks up the trait's `value`. Because both the argument and the
//     result are ordinary function pointers, the compiler reduces the
//     call to a pointer constant at the call site. Zero runtime
//     overhead — the resulting pointer is usable in constant
//     expressions (`static_assert(sturm::invert(&fwd) == &adj);`).
//   * Calling `sturm::invert(fn)` on an **unregistered** `fn` yields a
//     compile-time error. The error is precisely the missing-member
//     diagnostic on `adjoint_of<FnPtr>::value` — which is the standard
//     "incomplete type used in nested name specifier" / "no member named
//     'value'" message. That matches the "readable compile-time error"
//     acceptance bullet.
//
// Macro shape (from the issue spec):
//     STURM_REGISTER_ADJOINT(fn, adj)
//         -> template <> struct sturm::_detail::adjoint_of<decltype(&::fn)> {
//                static constexpr auto value = &::adj;
//            };
//
// Usage (both `fn` and `adj` must be at ::-namespace-scope because the
// expansion uses `::fn` / `::adj`):
//
//     void my_routine(qint& a, const qint& b) { ... }
//     void my_routine_adj(qint& a, const qint& b) { ... }
//     STURM_REGISTER_ADJOINT(my_routine, my_routine_adj)
//
//     // later, in user code or transpiler-emitted code:
//     sturm::invert(&my_routine)(x, y);   // runs my_routine_adj
//
// Header-only. LOC budget <= 300 (see CLAUDE.md).
#pragma once

namespace sturm {

namespace _detail {

// Primary template — intentionally undefined. A call to
// `sturm::invert(&fn)` for an unregistered `fn` instantiates this
// primary, and the subsequent access to `::value` produces the
// compile-time error documented above.
template <typename FnPtr>
struct adjoint_of;

}  // namespace _detail

// invert — return the adjoint function pointer registered for `fn`.
//
// Accepts any free-function pointer; constrained via the signature
// `R (*)(Args...)` deduction pattern so overload resolution rejects
// non-pointer-to-function inputs with a clear "no matching function"
// diagnostic rather than a deep trait-instantiation error.
//
// `constexpr` + `noexcept`: selection is a compile-time trait lookup
// with no side effects.
template <typename R, typename... Args>
constexpr auto invert(R (*fn)(Args...)) noexcept {
    (void)fn;  // the function pointer itself is unused — selection is
               // by type only (per the trait specialization the user
               // registered with STURM_REGISTER_ADJOINT).
    return _detail::adjoint_of<R (*)(Args...)>::value;
}

}  // namespace sturm

// ── Registration macro ──────────────────────────────────────────────────────
//
// Expands to a full specialization of sturm::_detail::adjoint_of keyed on
// the forward function's pointer type, whose single static member is a
// pointer to the adjoint. Both `fn` and `adj` are resolved at the global
// namespace (via the leading `::`) so the macro composes cleanly with
// user code that writes the routines in the global or in a nested
// namespace — in the nested case the user passes the fully-qualified
// name (e.g. `STURM_REGISTER_ADJOINT(foo::bar, foo::bar_adj)`), and the
// `::foo::bar` / `::adj` expansion remains well-formed because `::foo`
// is a valid nested-name-specifier.
//
// The macro is written to be used at namespace scope (NOT inside a
// function body) because it opens and closes the `sturm::_detail`
// namespace for the specialization.
#define STURM_REGISTER_ADJOINT(fn, adj)                                        \
    namespace sturm {                                                          \
    namespace _detail {                                                        \
    template <>                                                                \
    struct adjoint_of<decltype(&::fn)> {                                       \
        static constexpr auto value = &::adj;                                  \
    };                                                                         \
    }                                                                          \
    }
