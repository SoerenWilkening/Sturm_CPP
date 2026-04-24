// logic_dsl.hpp — M15 (PRD v3): Boolean logic in DSL style using qbool operators.
//
// Rewrites OR, NAND, NOR, XNOR using qbool operators (^=, &, |, flip()) instead
// of direct primitive calls. All functions in sturm:: namespace; no _when
// variants — WHEN lifting is automatic via the qbool/BitProxy operator path.
//
// Functions:
//   lib_or_dsl(a, b, c)   — OR:   c ^= (a | b)                  2 CX + 1 CCX
//   lib_nand_dsl(a, b, c) — NAND: c ^= (a & b); c.flip()        1 CCX + 1 X
//   lib_nor_dsl(a, b, c)  — NOR:  X(a); X(b); c^=(a&b); X(b); X(a)
//   lib_xnor_dsl(a, b, c) — XNOR: c ^= a; c ^= b; c.flip()
//
// Target: <120 LoC.

#pragma once

#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/routines/invert.hpp"

// Forward-declare BitProxy for the LO-1d adjoint registration (backend-only).
namespace sturm {
#ifdef STURM_BACKEND_ENABLED
struct BitProxy;
#endif
}  // namespace sturm

namespace sturm {

// ── lib_or_dsl ────────────────────────────────────────────────────────────────
//
// OR gate: c ^= (a | b).
// operator| feeds operator^=, which emits 2 CX + 1 CCX (3 gates).
//
// Precondition: c must be in |0> if you want c = a OR b after the call.
//               If c is not |0>, this XORs the OR result into c.
template <typename Bit>
inline void lib_or_dsl(Bit& a, Bit& b, Bit& c) {
    c ^= (a | b);   // operator^= path: CNOT(a,c) + CNOT(b,c) + CCX(a,b,c)
}

// ── lib_nand_dsl ──────────────────────────────────────────────────────────────
//
// NAND gate: c ^= (a & b); c.flip()
// = Toffoli(a,b,c) + X(c) = 2 gates.
//
// Precondition: c must be in |0> if you want c = NAND(a,b) after the call.
template <typename Bit>
inline void lib_nand_dsl(Bit& a, Bit& b, Bit& c) {
    c ^= (a & b);   // 1 CCX via operator^=
    c.flip();       // 1 X (or lifted under WHEN)
}

// ── lib_nor_dsl ───────────────────────────────────────────────────────────────
//
// NOR gate using De Morgan: NOR(a,b) = NOT(a) AND NOT(b).
//
// Steps:
//   1. a.flip()          — X(a)
//   2. b.flip()          — X(b)
//   3. c ^= (a & b)      — CCX(a,b,c) — c gets NOT(a_in) AND NOT(b_in) = NOR(a_in,b_in)
//   4. b.flip()          — X(b) restore
//   5. a.flip()          — X(a) restore
//
// Total: 2 X + 1 CCX + 2 X = 5 gates.
// a and b are restored to their original values.
//
// Precondition: c must be in |0> if you want c = NOR(a,b) after the call.
template <typename Bit>
inline void lib_nor_dsl(Bit& a, Bit& b, Bit& c) {
    a.flip();           // X(a): a becomes NOT(a)
    b.flip();           // X(b): b becomes NOT(b)
    c ^= (a & b);       // CCX: c ^= NOT(a_in) AND NOT(b_in) = NOR(a_in,b_in)
    b.flip();           // X(b): restore b
    a.flip();           // X(a): restore a
}

// ── lib_xnor_dsl ─────────────────────────────────────────────────────────────
//
// XNOR gate: c ^= a; c ^= b; c.flip()
// = CNOT(a,c) + CNOT(b,c) + X(c) = 3 gates.
//
// After the first two CX gates: c = a XOR b (if c started |0>).
// After the X gate: c = NOT(a XOR b) = XNOR(a,b).
//
// Precondition: c must be in |0> if you want c = XNOR(a,b) after the call.
template <typename Bit>
inline void lib_xnor_dsl(Bit& a, Bit& b, Bit& c) {
    c ^= a;     // CNOT(a, c)
    c ^= b;     // CNOT(b, c)
    c.flip();   // X(c): c = NOT(a XOR b) = XNOR(a,b)
}

// ── __lib_or_dsl_adj (LO-1d, sturm-le6w) ──────────────────────────────────────
// Adjoint of lib_or_dsl for the LO-2 rewrite's scope-exit cleanup
// (PRD §2.2/§2.3): reverse-order of `c ^= (a|b)` = `c^=a; c^=b; c^=(a&b)`.
// Each gate self-inverses and targets c, so applying the reversed sequence
// on c holding `a|b` returns c to |0>.  Registered only for the BitProxy
// instantiation consumed by the per-bit LO rewrite.
template <typename Bit>
inline void __lib_or_dsl_adj(Bit& a, Bit& b, Bit& c) {
    c ^= (a & b);   // CCX(a,b,c)
    c ^= b;         // CX(b,c)
    c ^= a;         // CX(a,c)
}

} // namespace sturm

#ifdef STURM_BACKEND_ENABLED
STURM_REGISTER_ADJOINT(sturm::lib_or_dsl<sturm::BitProxy>,
                       sturm::__lib_or_dsl_adj<sturm::BitProxy>)
#endif
