// PM3-5 negative-case (clean) input fixture for the
// `plugin_diagnostic_quantum_to_classical_cond_clean` CTest.
//
// Purpose
// -------
// Pin the NEGATIVE side of the PM3-5 triage: a `WHEN(q) { body }`
// invocation nested inside a classical `if (classical_flag) { ... }`
// MUST NOT raise the PM3-5 Error. The classical-condition slot holds
// a bare `bool` value and the qbool enters via the WHEN macro's
// own expanded three-`if` tower — which must be skipped via
// `detail::is_expansion_of_macro` so the WHEN tower is not mistaken
// for a user-written control construct.
//
// Shape
// -----
// A minimal hermetic `sturm::qbool` stub + WHEN macro mirroring the
// real `include/sturm/control/when.hpp:225-227` definition. The
// `demo(q, classical_flag)` function wraps the WHEN in a classical
// if; the classical `if (classical_flag)` branches on a `bool`
// (no cast, no quantum source), and the WHEN's inner tower is
// filtered out by the `is_expansion_of_macro` guard. No PM3-5
// diagnostic fires.
//
// Acceptance (per PM3-5 issue):
//   - sturm-transpile exits zero (no error).
//   - stderr does NOT contain the substring
//     `branch condition derives from quantum value via explicit cast`.
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    explicit operator bool() const { return false; }
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

void demo(qbool q, bool classical_flag) {
    // Classical branch (no cast, no quantum source) — PM3-5 must not
    // fire here. The WHEN's inner three-`if` tower appears inside the
    // expansion; the matcher's macro-skip guard must ignore that
    // tower so the WHEN itself is not mistaken for a user-written
    // control construct.
    if (classical_flag) {
        WHEN(q) { /* body intentionally empty */ }
    }
}
