// E7.M3 (sturm-va3z.3) positive-case input fixture: ++/-- shape. The
// E7.M2 spec's bullet list names BinaryOperator / CompoundAssignOperator
// / CXXOperatorCallExpr explicitly; the prose adds inc/dec via
// `UnaryOperator` for user-consistency with the sibling PM3-4 matcher.
// This fixture exercises the `UnaryOperator` arm of the WriteFinder so
// the integration suite covers the broadened shape set the E7.M2
// header documents.
//
// Expected stderr substring:
//     WHEN free variable 'tick' is mutated inside the WHEN body
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

qbool flag_for(int t) { return qbool(t >= 0); }

void demo(int tick) {
    WHEN(flag_for(tick)) { ++tick; }
}
