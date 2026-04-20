// PM2-8 input fixture for the `source_map_diagnostic` CTest.
//
// Purpose
// -------
// Drive the plugin pipeline with a deliberate type error INSIDE a
// transpiled region (the `WHEN(...)` argument) and pin Clang's diagnostic
// output to the user's source location. The test — wired via
// `tests/transpiler/check_source_map_diagnostic.cmake` — asserts that
// stderr contains `source_map_diagnostic_input.cpp:<user_line>:` and does
// NOT surface an `<memory-buffer>` pseudo-path. Those two negatives are
// the load-bearing signal that Phase-M's source-map pipeline (PM2-1..7) is
// end-to-end functional under the plugin driver:
//
//   - the plugin's nested CompilerInvocation preserves the user's
//     filename (via `getMemBufferCopy(rewritten, input_path)` in
//     `plugin.cpp:346`) so diagnostics do not degenerate to Clang's
//     anonymous `<memory-buffer>` fallback;
//   - `#line` directives emitted inside the rewritten buffer
//     (matcher_when_lift.cpp, matcher_qbool_compound.cpp,
//     uncompute_pass.cpp, …) anchor both user-source and
//     transpiler-synthesized statements back to the originating user
//     expression's line.
//
// Shape
// -----
// `WHEN((b | c) & 42) { (void)a; }` matches the plan-file example
// (`/home/agent/.claude/plans/i-want-to-remove-snug-galaxy.md`, Step 8) —
// an `int` literal (`42`) is passed where `qbool` is expected. `qbool`
// declares only `operator&(const qbool&, const qbool&)` (no implicit
// conversion from `int`, no `qbool(int)` ctor) so Clang's overload
// resolution rejects the expression with `invalid operands to binary
// expression ('qbool' and 'int')`. The error surfaces both on the parent
// parse and on the nested plugin parse of the rewritten buffer — in both
// cases at `source_map_diagnostic_input.cpp:<line-of-WHEN>:`, which is
// exactly what the harness asserts.
//
// The stub below is the hermetic Phase F PF-3/PF-4 preamble used by the
// `when_*.cpp` snapshot fixtures — `sturm::qbool` with `operator|` /
// `operator&`, the `materialize_when` overload pair, the 3-nested-`if`
// `WHEN(...)` macro. It keeps the fixture self-contained so the CTest
// does not need any `-I`/`--include-directory` flags and the plugin
// pipeline runs end-to-end from a single `clang++ -c` invocation — same
// shape as `tests/smoke/minimal_or.cpp` (the PL-3 smoke probe) and
// `when_compound.cpp` (the PF-4 byte-level golden input).
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

void demo(qbool a, qbool b, qbool c, qbool d) {
    WHEN((b | c) & 42) { (void)a; }
}
