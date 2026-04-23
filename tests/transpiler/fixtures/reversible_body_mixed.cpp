// reversible_body_mixed.cpp — Phase R / R-4 (sturm-88d7.5) positive
// straight-line fixture pinning the multi-primitive adjoint-synthesis
// contract.
//
// Canonical PRD §5.1 / P9c reversible-body shape
// ----------------------------------------------
// The routine carries `[[clang::annotate("sturm::reversible")]]` and its
// body is a straight-line (NO loops) sequence mixing THREE distinct
// primitive families to stress the adjoint_emitter dispatch table in a
// single pass:
//
//   *bregs[1] ^= *bregs[0];         // QOpKind::XOR_ASSIGN (qbool)
//   *iregs[0] += 3;                 // QOpKind::ADD_ASSIGN_CONST (qint)
//   (*iregs[0]).theta() += 0.5;     // QOpKind::THETA_ADD_ASSIGN_CONST
//   (*iregs[0]).phi() -= 0.25;      // QOpKind::PHI_SUB_ASSIGN_CONST
//   *iregs[0] *= 2;                 // QOpKind::MUL_ASSIGN_CONST
//   *bregs[1] ^= *bregs[0];         // QOpKind::XOR_ASSIGN (qbool)
//
// The body deliberately interleaves three kinds so the reversed-
// statement-order walk has to interleave three distinct per-kind
// render dispatches:
//
//   case XOR_ASSIGN         (uncompute_pass.cpp:108) — self-adjoint
//   case ADD_ASSIGN_CONST   (uncompute_pass.cpp:117) — `+=` ↔ `-=`
//   case MUL_ASSIGN_CONST   (uncompute_pass.cpp:132) — `*=` ↔ `/=`
//   case THETA_ADD_*        (uncompute_pass.cpp:144) — `+=` ↔ `-=`
//   case PHI_SUB_*          (uncompute_pass.cpp:178) — `-=` ↔ `+=`
//
// Phase R's `adjoint_emitter` (sturm-88d7.2) will machine-emit a
// sibling `__mixed_body_adj` function whose body is the six statements
// above in REVERSED source order, with each kind's sign-flip / self-
// adjoint rule applied per-statement:
//
//   *bregs[1] ^= *bregs[0];         // inverse of last XOR_ASSIGN (self)
//   *iregs[0] /= 2;                 // inverse of `*= 2`
//   (*iregs[0]).phi() += 0.25;      // inverse of phi `-= 0.25`
//   (*iregs[0]).theta() -= 0.5;     // inverse of theta `+= 0.5`
//   *iregs[0] -= 3;                 // inverse of `+= 3`
//   *bregs[1] ^= *bregs[0];         // inverse of first XOR_ASSIGN (self)
//
// The outer XOR_ASSIGN pair bracketing the body is symmetric — the
// first and last forward statements invert to themselves — but the
// middle four statements break the symmetry (constants and
// theta/phi angles differ).  This makes the fixture the sharpest
// multi-kind test of B11's reversed-statement-order invariant: any
// Phase R implementation that skipped the reversal would land the
// sign-flips in the WRONG order (e.g. `-= 3` before `+= 0.25` instead
// of after it) and the byte-compare would fail even though the
// outer frames still match.
//
// Why the pointer-array indirection is load-bearing
// -------------------------------------------------
// See `reversible_body_xor.cpp`, `reversible_body_compound.cpp`, and
// `reversible_body_rotation_{theta,phi}.cpp` for the per-kind
// rationale; the combined effect here is that NO existing transpiler
// matcher (PA-3, PB-1..4, PN-2a..d) fires on any body statement —
// every LHS anchor goes through `*regs[i]` which peels to a
// `UnaryOperator(ArraySubscriptExpr)` the matchers do not match.  The
// body is therefore a clean pass-through under the current
// transpiler.
//
// R-3 status (matcher_reversible_drive)
// -------------------------------------
// Per the R-4 issue (sturm-88d7.5) and the plan's §2.3 R-3/R-4 handoff
// contract, R-3 (matcher_reversible_drive) has landed but is not yet
// wired into `transpile_consumer.cpp`.  The transpile step for this
// fixture is therefore a PASS-THROUGH: sturm-transpile prepends the
// AUTO-GENERATED/Source header and copies the body verbatim.  The
// `.expected.cpp` golden differs from this input only by the two
// header lines.  When R-3 lands, the golden upgrades in place with
// the machine-emitted sign-flipped adjoint body + `STURM_REGISTER_
// ADJOINT(mixed_body, __mixed_body_adj);`.
//
// Primitive coverage
// ------------------
// Exercises `QOpKind::XOR_ASSIGN` + `ADD_ASSIGN_CONST` +
// `MUL_ASSIGN_CONST` + `THETA_ADD_ASSIGN_CONST` +
// `PHI_SUB_ASSIGN_CONST` in a single routine — the `_mixed` slot of
// the R-4 fixture set and the only fixture that exercises cross-kind
// interleaving in the adjoint_emitter dispatch.
//
// Stub qbool + qint_t — union of the other five fixtures' stubs.
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
};
template <int W>
class qint_t {
public:
    struct ThetaProxy {
        void operator+=(double) {}
        void operator-=(double) {}
    };
    struct PhiProxy {
        void operator+=(double) {}
        void operator-=(double) {}
    };
    qint_t() {}
    qint_t(const qint_t&) {}
    // NOLINTNEXTLINE(google-explicit-constructor)
    qint_t(long long) {}
    qint_t& operator+=(const qint_t&) { return *this; }
    qint_t& operator-=(const qint_t&) { return *this; }
    qint_t& operator*=(const qint_t&) { return *this; }
    qint_t& operator/=(const qint_t&) { return *this; }
    ThetaProxy theta() { return ThetaProxy{}; }
    PhiProxy   phi()   { return PhiProxy{}; }
};
} // namespace sturm
using sturm::qbool;
using qint = sturm::qint_t<1>;

[[clang::annotate("sturm::reversible")]]
void mixed_body(qbool& q0, qbool& q1, qint a) {
    qbool* bregs[2] = {&q0, &q1};
    qint*  iregs[1] = {&a};
    // Straight-line mixed-kind cascade — NO loops.  Six statements
    // across three primitive families: XOR_ASSIGN (qbool) bracketing
    // ADD_ASSIGN_CONST / THETA_ADD_ASSIGN_CONST /
    // PHI_SUB_ASSIGN_CONST / MUL_ASSIGN_CONST on the qint.  The body
    // is a clean pass-through under the current transpiler because
    // every LHS goes through a pointer-array dereference — PA-3,
    // PB-1..4 and PN-2a..d all require bare declRefExpr anchors.
    // The Phase R adjoint body is these six statements' per-kind
    // inverses in reversed source order.
    *bregs[1] ^= *bregs[0];
    *iregs[0] += 3;
    (*iregs[0]).theta() += 0.5;
    (*iregs[0]).phi() -= 0.25;
    *iregs[0] *= 2;
    *bregs[1] ^= *bregs[0];
}
