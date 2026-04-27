// E7.M3 (sturm-va3z.3) negative-case (clean) fixture for the
// `plugin_diagnostic_when_freevar_mutation_clean` CTest.
//
// Purpose
// -------
// Pin the negative side of E7's contract: a `WHEN(expr) { body }` whose
// body mutates ONLY body-local variables (declared inside the body)
// MUST NOT raise the E7.M2 hard-error. The body-local exclusion in
// `check_when_freevar_writes` keeps such mutations silent — they
// cannot corrupt the WHEN scope's read-set.
//
// Acceptance:
//   - sturm-transpile exits zero (no error).
//   - stderr does NOT contain the substring
//     `is mutated inside the WHEN body` (if it did, the matcher fired
//     on a body-local shape it should have rejected).
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

qbool predicate_for(int t) { return qbool(t > 0); }

void demo(int threshold) {
    // Body declares its own `local` and mutates only `local`. The
    // free variable `threshold` is read by the control expression
    // (it ends up in the read-set via the callee body) but is NOT
    // touched here, so no diagnostic should fire.
    WHEN(predicate_for(threshold)) {
        int local = 0;
        local = 7;
        local += 3;
        ++local;
    }
}
