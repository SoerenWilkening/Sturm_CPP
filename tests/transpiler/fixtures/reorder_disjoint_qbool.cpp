// Phase M / PM5-8 input for the sturm-transpile end-to-end snapshot
// test — "disjoint qbool" case.
//
// This is the happy-path end-to-end fixture for the PM5-5 peephole
// reorder matcher's decision tree. The matcher anchors on triples
// (A, B, C) where:
//   - A is `QOpKind::AND` with a synthetic `__stu_t*`-prefixed result
//     (produced by Phase E / PE-4 compound-flatten on a nested AND
//     sub-expression),
//   - C is `QOpKind::XOR_ASSIGN` whose first operand name matches
//     A's result,
//   - B is any reorderable op between them.
//
// Shape exercised here: the user writes a compound whose inner AND
// gets lifted into an `__stu_t0` synthetic temp. The canonical PM5
// trigger requires a subsequent `x ^= __stu_t0;` statement whose
// operand name equals A's result name. Because `__stu_t0` is a
// compiler-synthesized identifier, real user source cannot literally
// reference it — only a re-transpile path (or source that happens
// to mirror the synthetic convention) would hit the name-equality
// check. The fixture therefore captures the pipeline's byte-level
// emission for a representative nested-AND compound case, pinning
// the pre-reorder layout so any future enablement of a standalone
// AND matcher (which would push a `QOpKind::AND` op for a bare
// `qbool __stu_t = a & b;` decl and thereby make PM5 fire through
// user source) produces a visible snapshot delta.
//
// Demo shape:
//   qbool r = (a & b) | c;   // PE-4: pushes AND(__stu_t0, a, b) +
//                            //        OR(r, __stu_t0, c) into scope.ops
//   y ^= z;                   // B — disjoint XOR_ASSIGN
//   x ^= r;                   // C' — operand is `r`, not `__stu_t0`,
//                             //        so Gate 1's operand-name check
//                             //        rejects the triple
//   (void)r;                  // reader — PJ-4a dead-ancilla guard
//
// The stub is hermetic — only `operator&`, `operator|`, and
// `operator^=` need to resolve for the PE-4 compound-flatten + the
// PA-3 xor-assign matchers to fire.
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

void demo(qbool a, qbool b, qbool c, qbool x, qbool y, qbool z) {
    qbool r = (a & b) | c;
    y ^= z;
    x ^= r;
    (void)r;
}
