// invert.hpp — Compile-time `invert<&fn>()` + `STURM_REGISTER_ADJOINT` macro.
//
// Phase I PI-0 (docs/roadmap_transpiler_post_mvp.md). Ships the runtime
// machinery that principle P9 calls for: a free-function template
// `sturm::invert<&fn>()` that returns the adjoint registered for `fn`,
// and a `STURM_REGISTER_ADJOINT(fn, adj)` macro that records a
// forward/adjoint pair at namespace scope.
//
// ── Why NTTP keying (and not the function-pointer TYPE) ──────────────────────
//
// The trait `sturm::_detail::adjoint_of<>` is keyed on a *non-type* template
// parameter — the function-pointer VALUE — not on its TYPE. In C++20 a
// non-type template parameter declared `template <auto Fn>` accepts any
// structural value, including any free-function pointer. The literal value
// `&::lib_or_dsl<BitProxy>` and `&::lib_c_AND_dsl<BitProxy>` are two
// DIFFERENT NTTP values (different function addresses), so they instantiate
// two DIFFERENT specializations of `adjoint_of<>` even though both share
// the function-pointer TYPE `void(*)(BitProxy&, BitProxy&, BitProxy&)`.
//
// The earlier (pre-sturm-bdmh) macro keyed the trait on
// `decltype(&::fn)` — the function-pointer TYPE. This collides whenever two
// distinct function-template instantiations share a signature:
//
//     template <typename Bit> void lib_or_dsl   (Bit&, Bit&, Bit&);  // fwd
//     template <typename Bit> void lib_c_AND_dsl(Bit&, Bit&, Bit&);  // fwd
//     STURM_REGISTER_ADJOINT(lib_or_dsl<BitProxy>,    __lib_or_dsl_adj<BitProxy>)
//     STURM_REGISTER_ADJOINT(lib_c_AND_dsl<BitProxy>, __lib_c_AND_dsl_adj<BitProxy>)
//
// Both registrations expand to `template <> struct adjoint_of<void(*)(
// BitProxy&, BitProxy&, BitProxy&)> { ... }` — the SAME specialization with
// two different bodies. The compiler reports this as
// "redefinition of `adjoint_of<...>`". The fix is to key on the value:
// `template <> struct adjoint_of<&::lib_or_dsl<BitProxy>> { ... }` and
// `template <> struct adjoint_of<&::lib_c_AND_dsl<BitProxy>> { ... }` are
// two distinct specializations because the two pointer values differ.
//
// ── Why the call shape is `invert<&fn>()` (NTTP) and not `invert(&fn)` ──────
//
// A `consteval` overload `invert(auto fn)` cannot forward `fn` to
// `adjoint_of<fn>` because a `consteval` function parameter is NOT a
// constant expression inside its own body — it is still an ordinary
// runtime parameter for the purposes of template-argument substitution.
// The only portable C++20 way to get the function-pointer VALUE into a
// non-type template parameter is to take it as an NTTP at the call site:
//
//     sturm::invert<&fn>()(args)        // primary call shape
//
// (vs. the pre-sturm-bdmh `sturm::invert(&fn)(args)` with type-keyed
// trait). The migration is mechanical — every call site adds the `<& >`
// template-arg brackets and drops the function-pointer parameter.
//
// ── Design contract ──────────────────────────────────────────────────────────
//   * Trait-based dispatch. `sturm::_detail::adjoint_of<auto Fn>` is a
//     primary-undefined class template; `STURM_REGISTER_ADJOINT(fn, adj)`
//     expands to a full specialization that exposes
//         static constexpr auto value = &::adj;
//     The trait is the single source of truth for the fwd -> adj map.
//   * `sturm::invert<Fn>()` is a `constexpr` `noexcept` free function
//     template that looks up the trait's `value`. The result is a
//     constant expression (`static_assert(sturm::invert<&fwd>() == &adj)`)
//     and a runtime-callable function pointer
//     (`sturm::invert<&fwd>()(args)`).
//   * Calling `sturm::invert<Fn>()` for an **unregistered** `Fn` yields a
//     compile-time error. The error is the missing-member diagnostic on
//     `adjoint_of<Fn>::value` — incomplete-type / no-member-named-value —
//     which matches the "readable compile-time error" acceptance bullet.
//
// ── Macro shape ──────────────────────────────────────────────────────────────
//     STURM_REGISTER_ADJOINT(fn, adj)
//         -> template <> struct sturm::_detail::adjoint_of<&::fn> {
//                static constexpr auto value = &::adj;
//            };
//
// Usage (both `fn` and `adj` must be at ::-namespace-scope because the
// expansion uses `::fn` / `::adj`):
//
//     void my_routine    (qint& a, const qint& b) { ... }
//     void my_routine_adj(qint& a, const qint& b) { ... }
//     STURM_REGISTER_ADJOINT(my_routine, my_routine_adj)
//
//     // later, in user code or transpiler-emitted code:
//     sturm::invert<&my_routine>()(x, y);   // runs my_routine_adj
//
// Header-only. LOC budget <= 200 (see CLAUDE.md).
#pragma once

namespace sturm {

namespace _detail {

// Primary template — intentionally undefined. A use of
// `sturm::invert<&fn>()` for an unregistered `fn` instantiates this
// primary, and the subsequent access to `::value` produces the
// compile-time error documented above.
//
// Keyed on a *non-type* template parameter (the function-pointer VALUE)
// rather than on the TYPE so two function-template instantiations that
// share a signature — e.g. `lib_or_dsl<BitProxy>` and
// `lib_c_AND_dsl<BitProxy>`, both `void(*)(BitProxy&, BitProxy&, BitProxy&)` —
// can each register a distinct adjoint without colliding at the trait
// specialization level. See the file-level comment for the worked
// example that motivates the NTTP keying.
template <auto Fn>
struct adjoint_of;

}  // namespace _detail

// invert<Fn>() — compile-time adjoint lookup, keyed by NTTP.
//
// Returns the adjoint function pointer registered for the forward `Fn`.
// `constexpr` + `noexcept`: selection is a compile-time trait lookup
// with no side effects. The result is itself a function pointer, so
//
//     sturm::invert<&fn>()(args);
//
// resolves the adjoint at translation time (zero runtime overhead) and
// then calls it with the supplied arguments.
template <auto Fn>
constexpr auto invert() noexcept {
    return _detail::adjoint_of<Fn>::value;
}

}  // namespace sturm

// ── Registration macro ──────────────────────────────────────────────────────
//
// Expands to a full specialization of sturm::_detail::adjoint_of keyed on
// the forward function's pointer VALUE (not its type), whose single static
// member is a pointer to the adjoint. Both `fn` and `adj` are resolved at
// the global namespace (via the leading `::`) so the macro composes
// cleanly with user code that writes the routines in the global or in a
// nested namespace — in the nested case the user passes the
// fully-qualified name (e.g. `STURM_REGISTER_ADJOINT(foo::bar,
// foo::bar_adj)`), and the `::foo::bar` / `::foo::bar_adj` expansion
// remains well-formed because `::foo` is a valid nested-name-specifier.
//
// The macro is written to be used at namespace scope (NOT inside a
// function body) because it opens and closes the `sturm::_detail`
// namespace for the specialization.
//
// Note the NTTP-keyed shape: `adjoint_of<&::fn>` (the literal
// pointer value) rather than the pre-sturm-bdmh
// `adjoint_of<decltype(&::fn)>` (the pointer TYPE). Two function-template
// instantiations that share a signature — say `lib_or_dsl<BitProxy>` and
// `lib_c_AND_dsl<BitProxy>`, both `void(*)(BitProxy&, BitProxy&, BitProxy&)`
// — can each register a distinct adjoint because the two pointer values
// `&::lib_or_dsl<BitProxy>` and `&::lib_c_AND_dsl<BitProxy>` differ.
#define STURM_REGISTER_ADJOINT(fn, adj)                                        \
    namespace sturm {                                                          \
    namespace _detail {                                                        \
    template <>                                                                \
    struct adjoint_of<&::fn> {                                                 \
        static constexpr auto value = &::adj;                                  \
    };                                                                         \
    }                                                                          \
    }
