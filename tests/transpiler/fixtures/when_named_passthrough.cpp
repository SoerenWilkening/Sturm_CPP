// Phase F / PF-3 input for the sturm-transpile snapshot test.
//
// Exercises the named-passthrough WHEN path: `WHEN(named_qbool) { body }`
// where the macro argument peels (via `peel_to_payload`) to a bare
// `DeclRefExpr` to an existing named qbool. The PF-2 matcher's
// short-circuit kicks in before any rewrite is scheduled — no flat
// decl, no uncompute call, no argument replacement. The transpiler's
// output must therefore be BYTE-IDENTICAL to the input (modulo the
// two-line AUTO-GENERATED header the emitter always prepends).
//
// The fixture exists to pin that invariant: any regression that widens
// the lift to cover named arguments would produce an extra insertion
// and fail the byte-compare against the expected file. This is the
// simplest WHEN shape in the Phase F test suite and doubles as the
// canonical "do-not-rewrite" case for future matcher work.
//
// Stub is identical in shape to when_single_or.cpp's so the two
// fixtures exercise the same types against the two different paths.
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    bool should_run() const { return true; }
};

inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
inline qbool operator&(const qbool&, const qbool&) { return qbool{}; }

namespace detail {

inline qbool& materialize_when(qbool& q) { return q; }
inline qbool  materialize_when(qbool&& q) { return static_cast<qbool&&>(q); }

struct WhenCapture { WhenCapture() = default; };
inline qbool& make_when_guard(qbool& q) { return q; }

} // namespace detail
} // namespace sturm

using sturm::qbool;

#define WHEN(expr) \
    if (::sturm::detail::WhenCapture _when_capture_{}; true) \
    if (decltype(auto) _when_val_ = ::sturm::detail::materialize_when(expr); true) \
    if (auto& _when_guard_ = ::sturm::detail::make_when_guard(_when_val_); \
        _when_guard_.should_run())

void demo(qbool a, qbool b) {
    WHEN(a) { (void)b; }
}
