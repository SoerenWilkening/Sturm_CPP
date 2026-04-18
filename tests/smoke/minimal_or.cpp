// tests/smoke/minimal_or.cpp — Phase L PL-3 smoke fixture.
//
// Minimal qbool-OR shape consumed by the prebuilt-binary smoke-test matrix
// in .github/workflows/release.yml and by the Docker-image acceptance check
// in .github/workflows/release.yml's docker-publish job. The transpiler is
// expected to inject an `uncompute_or(tmp, a, b);` call before the
// enclosing scope's closing brace — the smoke test greps the emitted
// output file for that textual marker.
//
// Why this fixture is SELF-CONTAINED (no `#include <sturm/sturm.hpp>`)
// -------------------------------------------------------------------
// `sturm-transpile` silences diagnostics (see transpiler/src/main.cpp:542
// — it installs an `IgnoringDiagConsumer`). If the fixture depends on the
// real sturm headers but include paths happen to be wrong, the AST is
// degenerate, the M7 matcher's `hasType(cxxRecordDecl(hasName("qbool")))`
// never binds, and transpile produces a pass-through file with no
// `uncompute_or(...)` call — without ANY stderr signal. That silent-
// failure mode was exactly what bit the PL-7 Docker acceptance check:
// the image was correct, but this fixture's header chain was wrong for
// any environment that doesn't also define STURM_BACKEND_ENABLED and
// include `<sturm/control/when.hpp>` the way examples/or_circuit.cpp does.
//
// Keeping this file self-contained makes it a deterministic black-box
// smoke probe: the transpiler sees `qbool` as a real RecordDecl, the
// `a | b` as a CXXOperatorCallExpr against a free `operator|`, and the
// M7 matcher binds regardless of which include paths the caller passed.
// This mirrors tests/transpiler/fixtures/or_single.cpp (the PRD-MVP
// snapshot input), which has the same comment about why it inlines a
// stub rather than including the real header.
//
// Why `(void)tmp;` is load-bearing
// --------------------------------
// Phase J PJ-4a (dead-ancilla elimination) drops `qbool` VarDecls whose
// value is never read. Without a reader, PJ-4a fires BEFORE the M7
// uncompute-injection matcher: the whole `qbool tmp = a | b;` line is
// removed and the smoke-test's "output contains `uncompute_or(`" check
// fails. The explicit `(void)tmp;` bumps the reader count to 1, which
// disables PJ-4a for this VarDecl so the M7 matcher runs. Same rationale
// — and same textual shape — as tests/transpiler/fixtures/
// or_single_runtime.cpp (sturm-3dpf) and the PJ-3f / PJ-4c snapshot
// fixtures.
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(int) {}
    qbool(const qbool&) {}
};
inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
} // namespace sturm
using sturm::qbool;

void f() {
    qbool a{0}, b{0};
    qbool tmp = a | b;
    (void)tmp;
}
