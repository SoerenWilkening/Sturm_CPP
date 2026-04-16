// nested_when_runtime.cpp — Phase G / PG-7 transpiler INPUT fixture.
//
// Intended to be processed by `sturm-transpile` via an explicit
// add_custom_command (see tests/transpiler/CMakeLists.txt M12 block).
// The Phase G nested-WHEN matcher is expected to rewrite the doubly
// nested `WHEN(outer) { WHEN(inner) { ... } }` pair into:
//
//     WHEN(outer) {
//         qbool __stu_ctrl0 = outer & inner;
//         WHEN(__stu_ctrl0) {
//             target.flip();
//         }    sturm::uncompute_and(__stu_ctrl0, outer, inner);
//     }
//
// so the downstream compile emits a deterministic three-gate stream
// CCX(outer, inner, ctrl) + CX(ctrl, target) + CCX(outer, inner, ctrl)
// against the active BackendContext.  The hand-written companion
// `nested_when_reference.cpp` spells the same lowering verbatim; the
// M12 harness `test_gate_equivalence.cpp` byte-compares the two gate
// streams.
//
// Why the `__has_include` guard
// -----------------------------
// `sturm-transpile` runs Clang with a FixedCompilationDatabase that
// carries no include paths (see transpiler/src/main.cpp).  A bare
// `#include <sturm/sturm.hpp>` would therefore fail to resolve and
// the `qbool` + `WHEN` symbols would never appear in the AST, which
// would silently disable the Phase G matcher.  To make this TU
// parseable by the transpiler AND compilable at runtime, the include
// is gated on `__has_include`: during transpile the preprocessor
// takes the stub branch (which provides a minimal `sturm::qbool`,
// `materialize_when` overload set, and the exact three-`if` WHEN
// macro from include/sturm/control/when.hpp), and during the
// downstream compile the real headers are available so the
// transpiler-injected `sturm::uncompute_and(...)` call resolves via
// argument-dependent lookup.
//
// The stub shape mirrors the one used by when_nested_named.cpp /
// or_single_runtime.cpp: same class layout, same free operators,
// same WHEN macro expansion shape the matcher recognises.
//
// Namespace isolation
// -------------------
// The M12 harness links this TU together with nested_when_reference.cpp.
// Both define a `demo(const qbool&, const qbool&, qbool&)` function.
// Wrapping each in a dedicated namespace avoids ODR collision at link
// time; inside the namespace a `using sturm::qbool;` brings the qbool
// type into the local scope so the DSL pattern reads as it would to
// the end user.
//
// target ^= true semantics
// ------------------------
// The Phase G PG-7 issue describes the body as `target ^= true;`.
// There is no `qbool::operator^=(bool)` overload — the semantic
// equivalent that emits the expected single X (lifted under the
// active nested-control stack) is `target.flip()`, which calls
// `emit_X_lifted` and produces CX(__stu_ctrl, target) under a single
// active control.  The reference fixture performs the identical
// `target.flip()` call so the captured gate streams match gate-for-
// gate and qubit-index-for-qubit-index.
#if defined(__has_include) && __has_include(<sturm/sturm.hpp>)
#  include <sturm/sturm.hpp>
// The umbrella `sturm/sturm.hpp` pulls in the uncompute free-function
// API and the WHEN macro.  The qbool type and its flip() member live
// in qbool.hpp / qbool_ops.hpp, so include them explicitly here.
#  include "sturm/qtypes/qbool.hpp"
#  include "sturm/qtypes/qbool_ops.hpp"
#  include "sturm/control/when.hpp"
#else
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    bool should_run() const { return true; }
    qbool& flip() { return *this; }
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

#define WHEN(expr) \
    if (::sturm::detail::WhenCapture _when_capture_{}; true) \
    if (decltype(auto) _when_val_ = ::sturm::detail::materialize_when(expr); true) \
    if (auto& _when_guard_ = ::sturm::detail::make_when_guard(_when_val_); \
        _when_guard_.should_run())
#endif

namespace m12_nested_transpiled {
using sturm::qbool;

// Phase G nested-WHEN lift input: bare-DRE outer + bare-DRE inner.
// Matcher fires on the (outer, inner) pair and injects the
// `qbool __stu_ctrl0 = outer & inner;` decl before the inner WHEN,
// rewrites the inner arg to `__stu_ctrl0`, and plants
// `sturm::uncompute_and(__stu_ctrl0, outer, inner);` immediately
// after the inner WHEN body's closing brace.
// Note: WHEN(expr) calls `detail::materialize_when(expr)` which takes a
// non-const lvalue reference, so `outer` and `inner` cannot be `const
// qbool&` here.  `target` likewise is taken by non-const reference so
// `target.flip()` emits its lifted X against the active control stack.
// The caller (`run_and_capture_nested`) retains ownership via
// `qbool::make_non_owning`, so no qbool destructor will double-release.
void demo(qbool& outer, qbool& inner, qbool& target) {
    WHEN(outer) {
        WHEN(inner) {
            target.flip();
        }
    }
}

} // namespace m12_nested_transpiled
