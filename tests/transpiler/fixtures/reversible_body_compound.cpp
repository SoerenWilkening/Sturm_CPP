// reversible_body_compound.cpp — Phase R / R-4 (sturm-88d7.5) positive
// straight-line fixture pinning the classical-constant compound-assign
// adjoint-synthesis contract.
//
// Canonical PRD §5.1 / P9c reversible-body shape
// ----------------------------------------------
// The routine carries `[[clang::annotate("sturm::reversible")]]` and its
// body is a straight-line (NO loops) sequence of the Phase B classical-
// constant compound-assigns:
//
//   *regs[0] += 3;   // QOpKind::ADD_ASSIGN_CONST   → adj `-= 3`
//   *regs[0] -= 1;   // QOpKind::SUB_ASSIGN_CONST   → adj `+= 1`
//   *regs[0] *= 2;   // QOpKind::MUL_ASSIGN_CONST   → adj `/= 2`
//   *regs[0] /= 2;   // QOpKind::DIV_ASSIGN_CONST   → adj `*= 2`
//
// Phase R's `adjoint_emitter` (sturm-88d7.2) will machine-emit a
// sibling `__compound_body_adj` function whose body is the four
// inverse statements in REVERSED statement order (last-forward's
// inverse runs first):
//
//   a /= 2;   // inverse of forward's last stmt `a /= 2`  (was `*= 2`)
//   a *= 2;   // inverse of forward's 3rd stmt `a *= 2`   (was `/= 2`)
//
// wait — inverses:  +=3→-=3, -=1→+=1, *=2→/=2, /=2→*=2.
// In reverse statement order the adjoint body reads:
//
//   a *= 2;   // inverse of forward's last stmt (`/= 2`)
//   a /= 2;   // inverse of forward's third stmt (`*= 2`)
//   a += 1;   // inverse of forward's second stmt (`-= 1`)
//   a -= 3;   // inverse of forward's first stmt (`+= 3`)
//
// This is the sign-flipping pattern the Phase B inline-inverse arms of
// `uncompute_pass.cpp:render_uncompute` already produce today (cases
// ADD_ASSIGN_CONST, SUB_ASSIGN_CONST, MUL_ASSIGN_CONST,
// DIV_ASSIGN_CONST) — Phase R reuses those same render arms, walking
// the scope in REVERSE order instead of inserting inverses at scope
// close.  Per B11 the statement order is reversed.  This fixture has
// no loops, so the B11 loop-iteration reversal half does not apply.
//
// Why the pointer-array indirection is load-bearing
// -------------------------------------------------
// Phase B's PB-1..PB-4 matchers anchor the LHS on
// `declRefExpr(hasType(qint_t))` — a bare qint identifier.  A straight
// `a += 3;` would fire the matcher and plant `a -= 3;` at scope close,
// mutating the body.  Routing through `*regs[0] += 3;` gives the LHS a
// `UnaryOperator(ArraySubscriptExpr)` shape that the PB matchers do
// not match at all — the transpiler leaves the body verbatim.  Same
// workaround shape as the S-3 `reversible_loop_ripple.cpp` fixture
// uses for PA-3 / PH-3.
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
// ADJOINT(compound_body, __compound_body_adj);`.
//
// Primitive coverage
// ------------------
// Exercises `QOpKind::{ADD,SUB,MUL,DIV}_ASSIGN_CONST` — the `_compound`
// slot of the adjoint_emitter dispatch.  All four arms are sign-flip
// pairs: `+=` ↔ `-=`, `*=` ↔ `/=`.  The Phase R adjoint renderer
// reuses the Phase B inline-inverse render cases without modification.
//
// Stub qint_t — same minimal shape as `add_assign_const.cpp` plus the
// remaining three operator overloads so all four statements resolve.
namespace sturm {
template <int W>
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
    // NOLINTNEXTLINE(google-explicit-constructor)
    qint_t(long long) {}
    qint_t& operator+=(const qint_t&) { return *this; }
    qint_t& operator-=(const qint_t&) { return *this; }
    qint_t& operator*=(const qint_t&) { return *this; }
    qint_t& operator/=(const qint_t&) { return *this; }
};
} // namespace sturm
using qint = sturm::qint_t<1>;

[[clang::annotate("sturm::reversible")]]
void compound_body(qint a) {
    qint* regs[1] = {&a};
    // Straight-line classical-constant compound-assign cascade — NO
    // loops.  One of each kind so every adjoint_emitter dispatch arm
    // for QOpKind::{ADD,SUB,MUL,DIV}_ASSIGN_CONST is exercised.  Each
    // `*regs[0]` LHS is a `UnaryOperator(ArraySubscriptExpr)` so the
    // Phase B matchers do not fire.  The Phase R adjoint body is the
    // four inverse statements (sign flips / operator duals) in
    // reversed source order.
    *regs[0] += 3;
    *regs[0] -= 1;
    *regs[0] *= 2;
    *regs[0] /= 2;
}
