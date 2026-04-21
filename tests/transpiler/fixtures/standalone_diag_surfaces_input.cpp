// PM3-1 input fixture for the `transpile_diagnostic_surfaces` CTest.
//
// Purpose
// -------
// Pin the contract that the STANDALONE driver (`bin/sturm-transpile`)
// surfaces Clang diagnostics on stderr — not just the plugin. Prior to
// PM3-1 the standalone driver installed a `clang::IgnoringDiagConsumer`
// which swallowed every diagnostic (see the "Suppress diagnostics"
// comment at `transpiler/src/main.cpp:327`), making every downstream
// quantum-specific diagnostic (PM3-2..PM3-6) invisible unless the user
// went through the plugin path. This fixture + the companion CTest
// harness (`tests/transpiler/CMakeLists.txt`) assert that the driver
// now routes the DiagnosticsEngine through `TextDiagnosticPrinter` and
// the user sees diagnostic text on stderr.
//
// Shape
// -----
// A deliberate, language-level `static_assert(false, ...)` with a
// distinctive message string. We use `static_assert` (instead of a
// matcher-driven quantum diagnostic) so the test is:
//   - independent of any PM3-2..PM3-6 wiring (the plugin consumer's
//     custom diagnostic paths, the Phase-H reverse-loop warning, etc.);
//   - independent of any quantum type resolution (no `sturm::qbool`,
//     no `-I<include>` flag, no compile-commands.json);
//   - cheap to parse — a minimal TU triggers the diagnostic before the
//     transpiler's MatchFinder is even reached.
//
// The test harness greps stderr for the distinctive message substring
// (`STURM_PM3_DIAG_SURFACES_OK`). Presence of the substring proves the
// diagnostic reached stderr under the standalone driver — i.e.
// `IgnoringDiagConsumer` has been replaced by `TextDiagnosticPrinter`.
static_assert(false, "STURM_PM3_DIAG_SURFACES_OK");

int main() { return 0; }
