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

#include <array>
#include <cassert>
#include <cstddef>

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

// ── Phase S S-6 (sturm-ha2k.7) roundtrip fixtures ────────────────────────────
//
// Three in-place sweep routines whose adjoints are constructed by
// reversing the forward loop's iteration order (B11). Full end-to-end
// auto-registration of a synthesized adjoint (R-2 / auto_register_emitter)
// is NOT yet landed, so per the S-6 issue and the `sturm-ha2k.7` worker
// prompt we follow the prevailing test_invert convention: hand-write
// each adjoint as the byte-for-byte reversed-iteration twin of the
// forward, then register it via STURM_REGISTER_ADJOINT. The test
// asserts `forward(prep); invert(forward)(prep);` is the identity on
// the prepared state, which pins the Phase S loop-reversal contract
// and provides regression coverage against iteration-order drift.
//
// The three scenarios are the same ones covered by the S-3 positive
// loop fixtures (sturm-ha2k.4) and the S-5 m12 gate-equivalence
// pairs (sturm-ha2k.6):
//
//   1. ripple         — a ripple-style sweep reading its left neighbor.
//   2. bit_reverse    — swap bits i and n-1-i.
//   3. adder_carry    — classical ripple-carry adder whose carry
//                       propagates left-to-right on the forward pass
//                       and must be unwound right-to-left on the
//                       adjoint pass.

// Common register width for all three fixtures. Kept at 8 so the
// fixtures execute in O(1) per iteration and each loop body is small
// enough for the reversal contract to be obvious by inspection.
static constexpr std::size_t kN = 8;
using Reg = std::array<int, kN>;
using Carry = std::array<int, kN + 1>;

// Routines take their state registers by pointer so each fixture has
// a distinct function-pointer TYPE — STURM_REGISTER_ADJOINT keys on
// `decltype(&fn)`, and three `void()` functions would otherwise
// collide at the trait level. Passing pointers also keeps the
// fixtures reentrant (no hidden shared-globals coupling) which is
// important for Test 9's repeated-roundtrip pin.

// ── Fixture 1: ripple — in-place sweep, a[i] ^= a[i-1] ──────────────────
//
// Forward iterates i=1..N-1 and XORs each cell with its left
// neighbor. Because the neighbor itself is mutated by earlier
// iterations, the adjoint MUST walk i=N-1..1 — same XOR op (self-
// inverse at the bit level), reversed iteration order. This is the
// canonical B11 reversal contract.
void ripple(Reg* reg) {
    for (std::size_t i = 1; i < kN; ++i) {
        (*reg)[i] ^= (*reg)[i - 1];
    }
}

// Hand-written reversed-iteration adjoint. Iterating down via
// `std::size_t` requires a guard against wrap-around at zero; the
// body mirrors the forward body exactly.
void ripple_adj(Reg* reg) {
    for (std::size_t i = kN - 1; i >= 1; --i) {
        (*reg)[i] ^= (*reg)[i - 1];
    }
}
STURM_REGISTER_ADJOINT(ripple, ripple_adj)

// ── Fixture 2: bit_reverse — swap bits i and N-1-i via XOR triple ───────
//
// Forward iterates i=0..N/2-1 and swaps the i-th and (N-1-i)-th
// cells using the standard XOR-swap identity. Swaps are self-inverse
// and pairwise disjoint, so the adjoint is the same set of swaps in
// reversed iteration order — mirroring the B11 contract even though
// order is not strictly load-bearing here. The test pins that the
// reversed-loop adjoint is correct regardless.
//
// Distinct parameter list (`Reg*, int`) from `ripple` so the trait
// specialization keys on a different type.
void bit_reverse(Reg* reg, int /*tag*/) {
    for (std::size_t i = 0; i < kN / 2; ++i) {
        const std::size_t j = kN - 1 - i;
        (*reg)[i] ^= (*reg)[j];
        (*reg)[j] ^= (*reg)[i];
        (*reg)[i] ^= (*reg)[j];
    }
}

void bit_reverse_adj(Reg* reg, int /*tag*/) {
    // Reversed iteration via the standard unsigned-safe "post-
    // decrement pre-test" idiom. Statement order within the
    // iteration is also reversed — mirrors the B11 "reverse
    // statement order AND loop iteration order" contract. (XOR-swap
    // is symmetric, so either direction lands the same result, but
    // the adjoint emitter still reverses statements; we do the same
    // here so the test pins the contract end-to-end.)
    for (std::size_t i = kN / 2; i-- > 0; ) {
        const std::size_t j = kN - 1 - i;
        (*reg)[i] ^= (*reg)[j];
        (*reg)[j] ^= (*reg)[i];
        (*reg)[i] ^= (*reg)[j];
    }
}
STURM_REGISTER_ADJOINT(bit_reverse, bit_reverse_adj)

// ── Fixture 3: adder_carry — classical ripple-carry propagation ──────────
//
// Models the inner loop of a classical ripple-carry adder where the
// carry propagates left-to-right: each iteration reads `carry[i]`
// (which was written by iteration i-1) and writes `carry[i+1]`.
// That inter-iteration data dependency is exactly what forces the
// adjoint to walk the loop in reversed order. The adjoint also
// reverses the intra-iteration statement order (B11).
//
// Forward body (per iteration):
//     carry[i+1] ^= (a[i] & b[i])
//     carry[i+1] ^= (a[i] & carry[i])
//     carry[i+1] ^= (b[i] & carry[i])
//     s[i]       ^= a[i] ^ b[i] ^ carry[i]
//
// All ops are XOR-based and self-inverse at the bit level, so the
// adjoint body is the same four statements in reversed order, with
// the loop iterating i=N-1..0.
void adder_carry(const Reg* a, const Reg* b, Carry* carry, Reg* s) {
    for (std::size_t i = 0; i < kN; ++i) {
        (*carry)[i + 1] ^= ((*a)[i] & (*b)[i]);
        (*carry)[i + 1] ^= ((*a)[i] & (*carry)[i]);
        (*carry)[i + 1] ^= ((*b)[i] & (*carry)[i]);
        (*s)[i]         ^= (*a)[i] ^ (*b)[i] ^ (*carry)[i];
    }
}

void adder_carry_adj(const Reg* a, const Reg* b, Carry* carry, Reg* s) {
    // Reversed statement order within the reversed iteration —
    // B11 end-to-end. XORs are self-inverse bitwise, so applying
    // the same four lines in reversed order undoes the forward
    // step exactly IFF the outer loop also runs in reverse — which
    // it does.
    for (std::size_t i = kN; i-- > 0; ) {
        (*s)[i]         ^= (*a)[i] ^ (*b)[i] ^ (*carry)[i];
        (*carry)[i + 1] ^= ((*b)[i] & (*carry)[i]);
        (*carry)[i + 1] ^= ((*a)[i] & (*carry)[i]);
        (*carry)[i + 1] ^= ((*a)[i] & (*b)[i]);
    }
}
STURM_REGISTER_ADJOINT(adder_carry, adder_carry_adj)

// ── Phase R R-6 (sturm-88d7.7) straight-line roundtrip fixture ────────────────
//
// Phase R's mission is straight-line adjoint emission (no loops — those
// are Phase S's `loop_reversal` territory). The R-6 roundtrip test pins
// the PRD §8 invariant explicitly called out in the implementation plan:
//
//   "run forward then synthesized-adjoint on a prepared state; assert
//    state equals input and gate counter equals zero."
//                                     — PRD §8 item 3
//
// The fixture below is a four-statement XOR parity cascade — the
// canonical straight-line reversible shape from §5.1 of the PRD
// (`a ^= (x >= T)`-style body generalized to a multi-stmt cascade).
// Unlike the Phase S Tests 6–9 above, the body has NO for-loop; the
// statements are fully unrolled. The adjoint is produced by
// reverse-statement-order walking — the exact rewrite R-1's
// `adjoint_emitter` module (sturm-88d7.2) performs — and is hand-
// registered via STURM_REGISTER_ADJOINT because R-2
// (`auto_register_emitter`, sturm-88d7.3) feeds the PI-1 matcher but
// R-3 (`matcher_reversible_drive`, sturm-88d7.4) is not yet wired into
// `transpile_consumer.cpp`. When that wiring lands, the registration
// macro below is deleted in-place and `sturm::invert(&parity_cascade)`
// resolves through the machine-emitted `__parity_cascade_adj`.
//
// Gate-counter invariant
// ----------------------
// The test_invert harness operates on plain arrays — the fixtures do
// NOT go through a sturm::BackendContext, QubitPool, Sink or qbool
// dispatcher, so no backend gate_count() counter is meaningful here.
// We therefore adopt the same pattern Test 2 uses for `g_state`
// (counter-sink slots for fwd/adj): a file-local `g_gate_count`
// incremented by each forward statement and decremented by each
// adjoint statement. Because the adjoint is a reverse-statement-order
// mirror of the forward, the increments and decrements cancel to zero
// at the `forward ∘ adjoint` boundary. This is the test-local analog
// of the backend `ctx->gate_count` counter used by
// `tests/backend/test_lifted_primitives.cpp`: each fixture "emits" one
// counter bump per conceptual gate, and the adjoint must net them to
// zero. If the adjoint forgets a statement, drifts statement order,
// or double-applies any line, the counter is non-zero and the test
// fails. Per the R-6 issue prompt this is the "net zero gate emissions
// in observable state" pin.
namespace {
int g_gate_count = 0;
}  // namespace

// Forward body — four-statement straight-line XOR parity cascade.
// Each statement mutates one cell; the cascading reads-before-writes
// are exactly the pattern `adjoint_emitter` handles without loop
// reversal because the statements are already laid out in source
// order. The ++g_gate_count after each statement stands in for the
// backend's per-gate emission counter.
//
// The trailing `double tag` parameter is purely a type-disambiguator
// so the STURM_REGISTER_ADJOINT trait specialization keys on a
// function-pointer type distinct from `ripple`'s `void(Reg*)` (same
// rationale as `bit_reverse`'s `int` tag above). The tag is unused by
// the body — `(void)tag` suppresses the unused-parameter warning.
void parity_cascade(Reg* reg, double /*tag*/) {
    (*reg)[1] ^= (*reg)[0]; ++g_gate_count;
    (*reg)[2] ^= (*reg)[1]; ++g_gate_count;
    (*reg)[3] ^= (*reg)[2]; ++g_gate_count;
    (*reg)[4] ^= (*reg)[3]; ++g_gate_count;
}

// Reverse-statement-order adjoint — the exact shape R-1's
// `adjoint_emitter` produces for a straight-line reversible body. XOR
// is self-inverse at the bit level, so the adjoint body is the same
// statements in reversed source order. --g_gate_count mirrors the
// forward's ++g_gate_count, so forward ∘ adjoint nets to zero.
void parity_cascade_adj(Reg* reg, double /*tag*/) {
    (*reg)[4] ^= (*reg)[3]; --g_gate_count;
    (*reg)[3] ^= (*reg)[2]; --g_gate_count;
    (*reg)[2] ^= (*reg)[1]; --g_gate_count;
    (*reg)[1] ^= (*reg)[0]; --g_gate_count;
}
STURM_REGISTER_ADJOINT(parity_cascade, parity_cascade_adj)

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

    // ── Phase S S-6 roundtrip tests (sturm-ha2k.7) ───────────────────────
    //
    // For each fixture we:
    //   (a) capture a prepared (non-trivial) input state,
    //   (b) run the forward, confirming it actually mutated the state,
    //   (c) run the synthesized adjoint via `sturm::invert(&fwd)()`,
    //   (d) assert the state has returned to the prepared value —
    //       this is the `forward ∘ adjoint == identity` contract of B11.
    //
    // Per the S-6 issue (sturm-ha2k.7) and the worker prompt, R-2
    // (auto_register_emitter) is not yet landed, so `invert(&fwd)`
    // resolves via the hand-registered STURM_REGISTER_ADJOINT macros
    // above. The adjoint bodies are byte-for-byte reversed-iteration
    // twins of the forward bodies — the same shape the Phase S
    // `loop_reversal` module will emit automatically.

    // ── Test 6: ripple — roundtrip identity on a prepared register ────────
    {
        const Reg prep = {1, 0, 1, 1, 0, 0, 1, 0};
        Reg reg = prep;

        ripple(&reg);
        // Forward must mutate — else the test is vacuous.
        assert(reg != prep);

        sturm::invert(&ripple)(&reg);
        // forward ∘ adjoint = identity.
        assert(reg == prep);

        // Pin the invert(fn) pointer identity separately — the
        // result should be the registered adj at compile time.
        constexpr auto adj = sturm::invert(&ripple);
        static_assert(adj == &ripple_adj,
                      "invert(ripple) must return &ripple_adj");
    }

    // ── Test 7: bit_reverse — roundtrip identity on a prepared register ──
    {
        // Asymmetric payload so the swap is observable.
        const Reg prep = {7, 3, 5, 1, 2, 4, 6, 0};
        Reg reg = prep;

        bit_reverse(&reg, 0);
        // After the swap, the reg is literally the reverse of prep.
        Reg reversed{};
        for (std::size_t i = 0; i < kN; ++i) {
            reversed[i] = prep[kN - 1 - i];
        }
        assert(reg == reversed);

        sturm::invert(&bit_reverse)(&reg, 0);
        assert(reg == prep);

        constexpr auto adj = sturm::invert(&bit_reverse);
        static_assert(adj == &bit_reverse_adj,
                      "invert(bit_reverse) must return &bit_reverse_adj");
    }

    // ── Test 8: adder_carry — roundtrip identity on a prepared state ─────
    //
    // Pack two small numbers (a=0b10110101, b=0b11001010) into the
    // per-bit arrays. `s` and `carry` start at zero. Forward should
    // populate a non-zero sum; adjoint must return everything to the
    // prepared state (including the carry chain and the sum).
    {
        const Reg a_prep = {1, 0, 1, 0, 1, 1, 0, 1};  // a[0]=LSB
        const Reg b_prep = {0, 1, 0, 1, 0, 0, 1, 1};
        const Reg s_prep = {0, 0, 0, 0, 0, 0, 0, 0};
        const Carry c_prep{};                          // all zero

        Reg a = a_prep;
        Reg b = b_prep;
        Reg s = s_prep;
        Carry carry = c_prep;

        adder_carry(&a, &b, &carry, &s);
        // Sum or carry must be mutated — else the test is vacuous.
        assert(s != s_prep || carry != c_prep);

        sturm::invert(&adder_carry)(&a, &b, &carry, &s);
        // forward ∘ adjoint = identity on all four buffers.
        assert(a == a_prep);
        assert(b == b_prep);
        assert(s == s_prep);
        assert(carry == c_prep);

        constexpr auto adj = sturm::invert(&adder_carry);
        static_assert(adj == &adder_carry_adj,
                      "invert(adder_carry) must return &adder_carry_adj");
    }

    // ── Test 9: adder_carry — repeated roundtrips preserve state ─────────
    //
    // A second pin for B11: running forward+adjoint in a loop must
    // leave all four buffers at their prepared values after every
    // iteration (not just the first). Guards against the class of
    // bugs where a hand-rolled adjoint leaks one bit per cycle.
    {
        const Reg a_prep = {0, 1, 1, 0, 1, 0, 0, 1};
        const Reg b_prep = {1, 1, 0, 0, 0, 1, 1, 0};
        const Reg s_prep = {0, 0, 0, 0, 0, 0, 0, 0};
        const Carry c_prep{};

        Reg a = a_prep;
        Reg b = b_prep;
        Reg s = s_prep;
        Carry carry = c_prep;

        for (int k = 0; k < 5; ++k) {
            adder_carry(&a, &b, &carry, &s);
            sturm::invert(&adder_carry)(&a, &b, &carry, &s);
            assert(a == a_prep);
            assert(b == b_prep);
            assert(s == s_prep);
            assert(carry == c_prep);
        }
    }

    // ── Phase R R-6 (sturm-88d7.7) roundtrip test ─────────────────────────
    //
    // Pins the two invariants of the PRD §8 roundtrip gate for a
    // straight-line reversible body:
    //
    //   (1) forward ∘ synthesized_adjoint == identity on a prepared
    //       state, and
    //   (2) gate counter == 0 at scope exit.
    //
    // The fixture is `parity_cascade` — a four-statement XOR cascade
    // with NO for-loop (Phase R is straight-line; loops belong to
    // Phase S). Its adjoint is the reverse-statement-order mirror
    // R-1's `adjoint_emitter` will machine-produce once
    // `matcher_reversible_drive` (sturm-88d7.4) is wired into
    // `transpile_consumer.cpp`; today the adjoint is hand-registered
    // via STURM_REGISTER_ADJOINT above.
    //
    // The "gate counter == 0 at scope exit" pin is the load-bearing
    // piece of R-6: it asserts that the adjoint exactly mirrors the
    // forward's "emitted gates" — each forward statement's
    // ++g_gate_count is cancelled by its adjoint twin's
    // --g_gate_count. A missing, duplicated, or out-of-order adjoint
    // statement would leave g_gate_count non-zero and fail the test,
    // providing regression coverage for exactly the kind of drift
    // `adjoint_emitter` must guard against.
    {
        // Reset the test-local counter at scope entry so prior tests'
        // state cannot leak in. (None of them touch g_gate_count today,
        // but the reset is defensive against future additions.)
        g_gate_count = 0;

        // Non-trivial prepared state so the forward actually mutates —
        // else the test is vacuous.
        const Reg prep = {1, 0, 1, 1, 0, 0, 1, 0};
        Reg reg = prep;

        parity_cascade(&reg, 0.0);
        // Forward must mutate and must have bumped the gate counter
        // exactly four times (one per cascade statement).
        assert(reg != prep);
        assert(g_gate_count == 4);

        sturm::invert(&parity_cascade)(&reg, 0.0);
        // (1) forward ∘ adjoint = identity on the prepared register.
        assert(reg == prep);
        // (2) gate counter == 0 at scope exit — each forward
        //     ++g_gate_count is cancelled by its adjoint --g_gate_count
        //     mirror, nets to zero.
        assert(g_gate_count == 0);

        // Pin the invert(fn) pointer identity separately at compile
        // time — matches the constexpr-dispatch contract Tests 6/7/8
        // pin for the Phase S fixtures.
        constexpr auto adj = sturm::invert(&parity_cascade);
        static_assert(adj == &parity_cascade_adj,
                      "invert(parity_cascade) must return &parity_cascade_adj");
    }

    return 0;
}
