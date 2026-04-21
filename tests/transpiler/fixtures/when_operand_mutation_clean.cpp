// PM3-4 negative-case (clean) input fixture for the
// `plugin_diagnostic_when_operand_mutation_clean` CTest.
//
// Purpose
// -------
// Pin the NEGATIVE side of the PM3-4 triage: a `WHEN(expr) { body }`
// whose body mutates a qbool/qint that is NOT an operand of `expr`
// MUST NOT raise the PM3-4 Error. The matcher only flags mutations of
// identifiers that appear in the WHEN argument — unrelated mutations
// inside the body are legitimate (the body is expected to do work).
//
// Shape
// -----
// Identical to `when_operand_mutation_input.cpp` except the body
// declares a fresh `qbool c` and mutates `c ^= 1;`. `c` does not
// appear in the control expression `a | b`, so the operand set is
// `{a, b}` and the mutation check on `c` returns false — silent
// pass-through.
//
// Acceptance (per PM3-4 issue):
//   - sturm-transpile exits zero (no error).
//   - stderr does NOT contain the substring
//     `WHEN operand 'c' is mutated` (if it did, the matcher fired on a
//     shape it should have rejected).
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
    qbool& operator^=(int) { return *this; }
    bool should_run() const { return true; }
};

inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
inline qbool operator&(const qbool&, const qbool&) { return qbool{}; }

namespace detail {

inline qbool& materialize_when(qbool& q) { return q; }
inline qbool  materialize_when(qbool&& q) { return static_cast<qbool&&>(q); }

inline qbool& make_when_guard(qbool& q) { return q; }

} // namespace detail
} // namespace sturm

using sturm::qbool;

#define WHEN(expr) \
    if (decltype(auto) _when_val_ = ::sturm::detail::materialize_when(expr); true) \
    if (auto& _when_guard_ = ::sturm::detail::make_when_guard(_when_val_); \
        _when_guard_.should_run())

void demo(qbool a, qbool b) {
    // `c` is declared INSIDE the WHEN body and is NOT an operand of
    // the control expression `a | b`. The PM3-4 matcher must NOT
    // fire on `c ^= 1;` because `c` is absent from the operand set.
    WHEN(a | b) { qbool c; c ^= 1; }
}
