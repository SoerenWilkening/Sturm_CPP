// Phase M / PM5-8 input for the sturm-transpile end-to-end snapshot
// test — "plugin op refused" case.
//
// The PM5 plan §12 sharp-edge-3 pins a conservative refusal of
// commutation through `QOpKind::PLUGIN` ops. Plugin ops are opaque
// in v1 — the Registry has no footprint hook, so every operand of
// a plugin op would collapse to the universal footprint sentinel
// and `may_overlap()` would return true against every target. The
// PM5-5 matcher short-circuits at Gate 1 (`b_kind_is_reorderable`
// filter) to avoid that pointless Gate 3 pass.
//
// This fixture exercises the same PE-4 compound-flatten pattern as
// the other reorder_* fixtures, WITHOUT a plugin op in scope.ops —
// the transpiler's default build has no plugin registrars
// installed, so the `PLUGIN` kind never materialises through the
// bare-transpile path. The fixture therefore locks the
// "no-plugin-in-triple" output shape, pinning the byte-level
// emission that the snapshot would drift from if a future build
// inadvertently loaded a plugin that pushed a `QOpKind::PLUGIN`
// op between A and C.
//
// Shape exercised here:
//   qbool r = (a & b) | c;   // PE-4: AND(__stu_t0, ...), OR(r, ...)
//   (void)z;                  // B' — no QOp pushed (classical cast)
//   x ^= r;                   // C' — operand is `r`, Gate 1 rejects
//
// The snapshot captures the pipeline's pre-reorder emission. Any
// future enablement of the reorder on this file would produce a
// visible delta.
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
};
inline qbool operator&(const qbool&, const qbool&) { return qbool{}; }
inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
} // namespace sturm
using sturm::qbool;

void demo(qbool a, qbool b, qbool c, qbool x, qbool z) {
    qbool r = (a & b) | c;
    (void)z;
    x ^= r;
    (void)r;
}
