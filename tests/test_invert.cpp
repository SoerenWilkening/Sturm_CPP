// test_invert.cpp — Phase I PI-0 TDD tests for runtime invert() + the
// STURM_REGISTER_ADJOINT macro.
//
// See docs/roadmap_transpiler_post_mvp.md Phase I and bd sturm-hi66.
//
// Spec highlights the acceptance pins:
//   1. `invert(fn)` returns the adjoint function pointer registered by
//      STURM_REGISTER_ADJOINT(fn, adj).
//   2. Calling `invert(fwd)(args...)` invokes adj — verified by observing
//      that adj reverses the side effect of fwd on a shared counter sink.
//   3. The call is zero-runtime-overhead: `invert` is a `constexpr` free
//      function template selecting via a trait; we pin this with
//      static_asserts on the returned pointer being a compile-time constant.
//   4. Invoking `invert` on an unregistered function is a compile-time
//      error (missing specialization). Verified by manual comment — not a
//      runtime assertion — because that is by design a hard compile fail.
//
// The test deliberately uses free functions at namespace scope so that
// `STURM_REGISTER_ADJOINT(name, adj)` — which expands a specialization
// of `sturm::_detail::adjoint_of<decltype(&::name)>` — resolves `::name`
// unambiguously.

#include "sturm/routines/invert.hpp"

#include <cassert>

// ── Shared counter sink — each call bumps a specific slot ────────────────────
//
// We prove round-trip by asserting `fwd_calls == adj_calls` after we call
// `invert(fwd)` through its registered adjoint.

namespace {
int g_fwd_calls = 0;
int g_adj_calls = 0;
int g_state     = 0;  // mutable shared state — fwd adds, adj subtracts
}  // namespace

// Forward routine: increments state by `delta`.
void demo_fwd(int delta) {
    g_state += delta;
    ++g_fwd_calls;
}

// Manual adjoint: decrements state by `delta` — the dual of demo_fwd.
void demo_adj(int delta) {
    g_state -= delta;
    ++g_adj_calls;
}

// A second routine pair with a different signature to prove the trait
// specializes per-function-pointer-type, not per call.
int demo_fn_b(int a, int b) { return a + b; }
int demo_fn_b_adj(int a, int b) { return a - b; }

// Register both pairs.
STURM_REGISTER_ADJOINT(demo_fwd, demo_adj)
STURM_REGISTER_ADJOINT(demo_fn_b, demo_fn_b_adj)

// A routine that is intentionally NOT registered — invert(demo_unreg)
// must fail to compile. The line below stays commented out because the
// acceptance criterion is "readable compile-time error"; we cannot
// runtime-assert a compile error. Uncomment locally to manually verify.
//
//   int demo_unreg(int x) { return x; }
//   static auto check = sturm::invert(&demo_unreg);   // should not compile

int main() {
    // ── Test 1: invert(fwd) returns the registered adj pointer ───────────
    {
        constexpr auto adj_ptr = sturm::invert(&demo_fwd);
        // Pin the "zero runtime overhead / compile-time dispatch" claim:
        // the result is a constexpr, so it must be usable in a constant
        // expression context.
        static_assert(adj_ptr == &demo_adj,
                      "invert(demo_fwd) must return &demo_adj at compile time");
        assert(adj_ptr == &demo_adj);
    }

    // ── Test 2: invoking invert(fwd)(args...) runs adj ───────────────────
    {
        g_fwd_calls = 0;
        g_adj_calls = 0;
        g_state     = 0;

        demo_fwd(5);                   // state: 0 -> 5, fwd_calls=1
        assert(g_state == 5);
        assert(g_fwd_calls == 1);
        assert(g_adj_calls == 0);

        sturm::invert(&demo_fwd)(5);   // state: 5 -> 0, adj_calls=1
        assert(g_state == 0);          // round-trip proven
        assert(g_fwd_calls == 1);
        assert(g_adj_calls == 1);
    }

    // ── Test 3: multiple round-trips preserve state ──────────────────────
    {
        g_state = 42;
        for (int i = 1; i <= 10; ++i) {
            demo_fwd(i);
            sturm::invert(&demo_fwd)(i);
        }
        assert(g_state == 42);
    }

    // ── Test 4: the trait specializes per-function-pointer — invert on
    //           a second registered routine returns its own adjoint ──────
    {
        constexpr auto adj_b = sturm::invert(&demo_fn_b);
        static_assert(adj_b == &demo_fn_b_adj,
                      "invert(demo_fn_b) must return &demo_fn_b_adj");
        assert(adj_b(10, 3) == 7);     // adj = subtraction
        assert(demo_fn_b(10, 3) == 13); // fwd = addition
    }

    // ── Test 5: invert composes as an involution for self-inverse ops ────
    //           (demo_fwd and demo_adj are NOT self-inverse, but the
    //           returned pointer is itself a plain function pointer and
    //           therefore usable wherever a function pointer is expected.)
    {
        void (*p)(int) = sturm::invert(&demo_fwd);
        assert(p == &demo_adj);
    }

    return 0;
}
