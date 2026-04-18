// tests/smoke/minimal_or.cpp — Phase L PL-3 smoke fixture.
//
// Dependency-free minimal qbool-OR shape consumed by the prebuilt-binary
// smoke-test matrix in .github/workflows/release.yml. The transpiler is
// expected to inject an `uncompute_or(tmp, a, b);` call before the
// enclosing scope's closing brace — the smoke test greps the emitted
// output file for that textual marker.
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
//
// This file is NOT a tests/transpiler/ fixture: it lives at tests/smoke/
// specifically so the release smoke-test matrix can reference it from a
// fresh checkout without pulling in any of the broader test harness.
#include <sturm/sturm.hpp>

void f() {
    qbool a{0}, b{0};
    qbool tmp = a | b;
    (void)tmp;
}
