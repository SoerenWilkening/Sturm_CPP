// Phase M / PM5-8 input for the sturm-transpile end-to-end snapshot
// test — "disjoint qint bits" case.
//
// Mirror of reorder_disjoint_qbool.cpp but the B statement is a qint
// compound-assign on a qint_t<4> local `arr`. The alias extractor
// should report `arr`'s footprint as `{name="arr", bit_range=[0, 4)}`
// (a full-width qint footprint), bit-disjoint from any qbool's
// footprint. When an AND op + matching XOR_ASSIGN triple is present
// in `scope.ops`, the matcher's Gate 3 footprint-disjointness check
// admits a qint-bits B commuting past a qbool-C.
//
// Shape exercised here:
//   qint_t<4> arr = 0;                 // qint decl — footprint [0,4)
//   qbool r = (a & b) | c;             // PE-4 pushes AND(__stu_t0, ...) +
//                                      //            OR(r, __stu_t0, c)
//   arr += 1;                          // B  — ADD_ASSIGN_CONST on qint
//   x ^= r;                            // C' — XOR_ASSIGN(x, r);
//                                      //     operand is `r`, not `__stu_t0`,
//                                      //     so Gate 1's name check rejects.
//   (void)r;
//
// Through the current matcher set PM5 does not fire — the same Gate 1
// operand-name discriminator rejects the triple that reorder_disjoint_
// qbool documents. The snapshot captures the pre-reorder pipeline
// emission so any future enablement of a standalone AND matcher (that
// pushes QOpKind::AND ops for bare `qbool __stu_t = a & b;` decls)
// produces a visible snapshot delta here.
//
// The stub adds a minimal qint_t<W> stub on top of qbool's so the
// `arr += 1;` anchor resolves. sturm-transpile runs with no include
// paths, so every type the matchers consult must be declared in this
// TU.
#include <cstddef>
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

template <std::size_t W>
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
    // NOLINTNEXTLINE(google-explicit-constructor)
    qint_t(long long) {}
    qint_t& operator=(const qint_t&) { return *this; }
    qint_t& operator+=(int) { return *this; }
};
} // namespace sturm
using sturm::qbool;
using qint4_t = sturm::qint_t<4>;

void demo(qbool a, qbool b, qbool c, qbool x, qbool y) {
    qint4_t arr = 0;
    qbool r = (a & b) | c;
    arr += 1;
    x ^= r;
    (void)r;
    (void)arr;
    (void)y;
}
