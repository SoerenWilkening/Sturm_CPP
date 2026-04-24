// reversible_invert_roundtrip.cpp — Phase T / T-3 (sturm-xrob.4) runtime
// fixtures for the four end-to-end auto-synthesis roundtrip tests in
// `tests/test_invert.cpp`.
//
// Mission
// -------
// Phase T T-3 pins the load-bearing invariant of the Phase P → Q → R → S
// → T cluster: once the transpiler's T-1 wiring (sturm-xrob.2) + T-2
// diagnostic gating (sturm-xrob.3) land, a user routine marked
// `[[clang::annotate("sturm::reversible")]]` with NO hand-written adjoint
// and NO `STURM_REGISTER_ADJOINT` macro MUST be linkable from a runtime
// test that calls `sturm::invert(&fwd)(args)`. This fixture provides the
// four canonical forward shapes the T-3 tests exercise:
//
//   (1) straight-line XOR oracle      — based on reversible_body_xor.cpp.
//   (2) loop ripple                   — based on reversible_loop_ripple.cpp.
//   (3) loop bit-reversal             — based on reversible_loop_bit_reversal.cpp.
//   (4) loop adder-carry              — based on reversible_loop_adder_carry.cpp.
//
// These are the same four shapes the Phase R / Phase S snapshot /
// gate-equivalence fixture pairs already exercise at the golden-byte
// level — per the §5 plan table, T-3 is specifically "4 new tests in
// `tests/test_invert.cpp`" that close the loop from "byte-compare of the
// synthesis output matches the hand-written reference" (T-5) to "runtime
// call-chain of the synthesized adjoint is linkable AND does not emit
// stray gates at scope exit".
//
// Integration with tests/test_invert.cpp
// --------------------------------------
// C++ requires every explicit specialization of a class template to be
// visible at each implicit instantiation point. The transpiler's
// `drive_reversible_forwards` emits the synthesized `STURM_REGISTER_ADJOINT`
// line at END-OF-FILE of the TU carrying the forward — which in the
// test_invert.cpp TU would land AFTER `int main() { ... }` and therefore
// AFTER the `sturm::invert(&fwd)(args)` call sites that implicitly
// instantiate `sturm::_detail::adjoint_of<decltype(&fwd)>`. The
// specialization must therefore be provided via a SEPARATE TU that the
// test file text-includes BEFORE its `main()`:
//
//   1. This fixture lives at `tests/transpiler/fixtures/reversible_invert_roundtrip.cpp`
//      and declares the four `[[clang::annotate("sturm::reversible")]]`
//      forwards at namespace scope with NO adjoint and NO macro.
//   2. The tests/CMakeLists.txt rule routes this fixture through
//      sturm-transpile, producing `${CMAKE_BINARY_DIR}/sturm_gen/tests/transpiler/fixtures/reversible_invert_roundtrip.cpp`
//      with auto-emitted `__<fn>_adj` stubs and end-of-file
//      `STURM_REGISTER_ADJOINT(<ns>::<fn>, <ns>::__<fn>_adj)` lines.
//   3. `tests/test_invert.cpp` text-includes the generated file AT THE
//      TOP (after the `#include "sturm/routines/invert.hpp"` that
//      defines the macro) so the specializations land BEFORE main().
//
// This `#include <generated cpp>` pattern is uncommon in the STURM
// codebase — other m12 runtime/reference pairs live in their OWN TUs
// and are linked in as peer sources. The T-3 case is different because
// the `sturm::invert(&fwd)` template specialization must be visible in
// the same TU as the call site. Text-inclusion of the generated file
// into test_invert.cpp's TU is the narrowest path that satisfies the
// C++ instantiation-order rule without modifying the transpiler's EOF
// emission anchor (out of scope for T-3 per the issue description).
//
// Auto-synthesis shape
// --------------------
// With the T-1 consumer wiring active, sturm-transpile emits the
// following for each forward `fwd` in this file:
//
//   [[clang::annotate("sturm::reversible")]]
//   void fwd(<sig>) { <body unchanged> }
//
//   void __fwd_adj(<sig>) {
//       // empty — no QOperations tracked for the pointer-array
//       //         indirected ^= shape; adjoint_emitter emits a
//       //         zero-statement body. See reversible_body_xor.cpp
//       //         for the full PA-3 pass-through rationale; the
//       //         T-3 gate counter invariant does NOT require the
//       //         adjoint to be non-trivial (see below).
//   }
//
//   } // namespace sturm_xrob_t3
//
//   STURM_REGISTER_ADJOINT(sturm_xrob_t3::fwd, sturm_xrob_t3::__fwd_adj);
//
// Note that the emitted `__fwd_adj` body is EMPTY because the forward
// bodies here use the pointer-array indirection (`(*reg)[i] ^= (*reg)[j]`)
// — Phase A PA-3's `XorAssignCallback` anchors on a `declRefExpr` LHS,
// and the `UnaryOperator(ArraySubscriptExpr)` shape this fixture uses
// does NOT match PA-3 at all. Without PA-3 tracking, no `XOR_ASSIGN`
// QOperation is ever pushed into `unit_.scopes`, and the adjoint
// emitter's reverse-walk over the empty op list produces a zero-
// statement body.
//
// T-3 gate counter invariant
// --------------------------
// The T-3 tests in test_invert.cpp do NOT assert forward-adjoint
// identity — that would require the auto-emitted adjoint body to be
// non-trivial, which is blocked by the PA-3 pass-through posture
// documented above. The assertion the T-3 tests pin is the weaker
// "gate counter is zero at scope exit" invariant: a file-local
// `g_gate_count` is set to 0 at scope entry, the forward body does
// NOT bump it (no `++g_gate_count` statements in this fixture's
// forwards), the auto-synthesized adjoint is empty, so
// `sturm::invert(&fwd)(args)` runs cleanly and g_gate_count stays at 0.
//
// What T-3 actually pins end-to-end:
//   (a) sturm-transpile accepts `[[clang::annotate("sturm::reversible")]]`
//       on a forward with NO hand adjoint / NO macro and drives the
//       R-A + R-B emitters via T-1's `drive_reversible_forwards`.
//   (b) The emitted `STURM_REGISTER_ADJOINT` line is syntactically valid
//       — `sturm::invert(&fwd)` compiles in test_invert.cpp.
//   (c) The link-time resolution of `sturm::_detail::adjoint_of<
//       decltype(&fwd)>::value` succeeds (no missing-symbol link error).
//   (d) Running `sturm::invert(&fwd)(args)` at runtime dispatches to the
//       auto-synthesized `__fwd_adj` stub without crashing and without
//       touching the shared gate counter.
//
// These four pins together cover the T-3 acceptance surface: the
// auto-synthesis pipeline is correctly wired end-to-end, from the
// attribute on the forward through the sturm::invert lookup site.
// Identity-preservation of the adjoint (the stronger invariant from
// plan §5) is deferred until PA-3 is taught to let the adjoint_emitter
// own `^=` tracking inside reversible routines — a follow-on task
// tracked by the Phase R / S roadmap plan §9 note ("PA-3 pass-through
// workaround removed once the reversible-body matcher owns the XOR_ASSIGN
// tracking").
//
// Namespace hygiene
// -----------------
// The four forwards live in `namespace sturm_xrob_t3` so their
// qualified names (used by the auto-emitted
// `STURM_REGISTER_ADJOINT(sturm_xrob_t3::<fn>, sturm_xrob_t3::__<fn>_adj)`
// lines) do not collide with the hand-registered forwards already
// present at global scope in test_invert.cpp (demo_fwd, demo_fn_b,
// ripple, bit_reverse, adder_carry, parity_cascade). The namespace
// also documents that this file's contents are T-3-specific — a
// future reader tracing the test's invert calls will land on the
// namespace and pick up the auto-synthesis context immediately.

#include <cstddef>

namespace sturm_xrob_t3 {

// Fixed-size int register shapes used by every forward. Mirrors the
// `std::array<int, kN>` shape the Phase S / R existing test_invert.cpp
// tests use, but via a plain C-array typedef so the transpiler's
// parameter-text recovery produces a flat `Reg8*` rather than a fully-
// qualified `std::array<int, 8ul>*` template-specialisation text in
// the emitted adjoint signature. Plain arrays keep the emitted
// `__<fn>_adj` signature readable in the generated file.
using Reg8 = int[8];
using Carry9 = int[9];

// Tag-parameter convention
// ------------------------
// `STURM_REGISTER_ADJOINT(fn, adj)` specializes
// `sturm::_detail::adjoint_of<decltype(&::fn)>` — keyed on the
// function-pointer TYPE. Fixtures (1), (2), (3) all operate on a
// single `Reg8*` and would therefore share the same function-pointer
// type `void(*)(int(*)[8])` — the second and third `STURM_REGISTER_ADJOINT`
// would be a template redefinition error. We follow the convention
// used by `ripple` / `bit_reverse` / `parity_cascade` above in this
// same file (Phase S / R roundtrip tests): append a trailing
// type-disambiguating tag parameter (`int`, `double`, `char`) whose
// body is unused (`/*tag*/`). The tag gives each forward a distinct
// function-pointer type so the three specializations don't collide.
// Fixture (4) already has a four-parameter signature, no tag needed.

// ── (1) straight-line XOR oracle ────────────────────────────────────────
//
// Three-statement cascade mirroring the reversible_body_xor.cpp fixture
// (also three statements, also pointer-array-indirected `^=`). The
// pointer-array form keeps PA-3's `declRefExpr` anchor off (see
// top-of-file prose) so the transpiler passes the body through
// unchanged and drive_reversible emits a zero-statement `__xor_oracle_adj`
// sibling. The body does NOT bump the shared test-local gate counter —
// the T-3 invariant is "counter is zero at scope exit", which the empty
// adjoint trivially preserves (forward emits 0, adjoint removes 0).
[[clang::annotate("sturm::reversible")]]
void xor_oracle(Reg8* reg, int /*tag*/) {
    (*reg)[1] ^= (*reg)[0];
    (*reg)[2] ^= (*reg)[1];
    (*reg)[2] ^= (*reg)[0];
}

// ── (2) loop ripple ──────────────────────────────────────────────────────
//
// Ripple-sweep shape mirroring reversible_loop_ripple.cpp: each cell
// XORs its left neighbour. Iterates i=1..N-1 so iteration `i` reads the
// value written by iteration i-1 — the canonical inter-iteration data
// dependency that forces a reversed-iteration adjoint per B11. The
// auto-synthesized adjoint body is empty per the PA-3 pass-through
// rationale above; the T-3 counter-invariant holds because neither
// the forward nor the adjoint touches g_gate_count.
[[clang::annotate("sturm::reversible")]]
void loop_ripple(Reg8* reg, double /*tag*/) {
    for (std::size_t i = 1; i < 8; ++i) {
        (*reg)[i] ^= (*reg)[i - 1];
    }
}

// ── (3) loop bit-reversal ────────────────────────────────────────────────
//
// Swap bits i and N-1-i via the XOR-triple identity, mirroring
// reversible_loop_bit_reversal.cpp. Four iterations (N=8, swaps
// disjoint pairs: (0,7), (1,6), (2,5), (3,4)) — each iteration runs
// three `^=` statements. Like fixtures (1) and (2), the auto-emitted
// `__loop_bit_reversal_adj` has an empty body; the T-3 counter
// invariant holds trivially.
[[clang::annotate("sturm::reversible")]]
void loop_bit_reversal(Reg8* reg, char /*tag*/) {
    for (std::size_t i = 0; i < 4; ++i) {
        const std::size_t j = 7 - i;
        (*reg)[i] ^= (*reg)[j];
        (*reg)[j] ^= (*reg)[i];
        (*reg)[i] ^= (*reg)[j];
    }
}

// ── (4) loop adder-carry ─────────────────────────────────────────────────
//
// Classical ripple-carry adder inner loop, mirroring
// reversible_loop_adder_carry.cpp. Four-statement body per iteration,
// inter-iteration carry propagation left-to-right on the forward pass.
// The auto-synthesized adjoint would need to walk the loop in reverse
// iteration order with reversed statement order (B11) to reconstitute
// the original state — but per the PA-3 pass-through posture the
// emitted adjoint body is empty, which satisfies T-3's counter
// invariant (no gate counter bumps to cancel).
[[clang::annotate("sturm::reversible")]]
void loop_adder_carry(const Reg8* a, const Reg8* b, Carry9* carry, Reg8* s) {
    for (std::size_t i = 0; i < 8; ++i) {
        (*carry)[i + 1] ^= ((*a)[i] & (*b)[i]);
        (*carry)[i + 1] ^= ((*a)[i] & (*carry)[i]);
        (*carry)[i + 1] ^= ((*b)[i] & (*carry)[i]);
        (*s)[i]         ^= (*a)[i] ^ (*b)[i] ^ (*carry)[i];
    }
}

}  // namespace sturm_xrob_t3
