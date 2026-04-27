// E7.M3 (sturm-va3z.3) positive-case input fixture: compound-assign
// shape. Exercises the `CompoundAssignOperator` arm of the E7.M2
// `WriteFinder` — the body mutates a free variable of the WHEN
// control expression via `+=`. Drives the same matcher pipeline as
// `when_freevar_mutation_input.cpp` (plain `=`) so both AST shapes
// are pinned in the integration suite.
//
// Expected stderr substring:
//     WHEN free variable 'counter' is mutated inside the WHEN body
// AND exit code non-zero.
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(bool) {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    bool should_run() const { return true; }
};

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

qbool gate_for(int n) { return qbool(n != 0); }

void demo(int counter) {
    WHEN(gate_for(counter)) { counter += 5; }
}
