// fixture.cpp — PM4-5 CMake glue fixture for
// add_quantum_executable(... PLUGINS ...).
//
// Used by tests/transpiler/test_cmake_plugins/ to exercise the PM4-5
// extension to `cmake/SturmTranspile.cmake`'s `add_quantum_executable()`
// helper. Sibling of tests/transpiler/test_cmake_glue/src/fixture.cpp —
// kept separate so the two fixtures' generated-sibling paths do not
// collide and the two tests can fail independently.
//
// Deliberately minimal: no <sturm/sturm.hpp> include, no quantum ops.
// The goal is to validate the PM4-5 build-system glue — specifically
// that `PLUGINS` paths end up as per-source
// `-Xclang -plugin-arg-sturm-transpile -Xclang load=<abs-path>`
// cc1 arg pairs on the Clang command line (NOT that the demo plugin
// runtime-registers anything; PM4-8's smoke test does that end-to-end).
// A plain C++ program is sufficient because the transpiler still runs
// over it, the matcher finds nothing, and the build succeeds as long
// as the per-source compile options wire up correctly.

int main() {
    // STURM_PLUGINS_FIXTURE_MARKER identifies this fixture in any grep
    // over the generated-sibling artefact. Unique enough that an
    // accidental match is vanishingly unlikely.
    const int sturm_plugins_fixture_marker = 0xA512;
    return sturm_plugins_fixture_marker - 0xA512;
}
