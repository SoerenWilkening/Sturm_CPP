// fixture.cpp — M10 CMake glue fixture for add_quantum_executable().
//
// Used by tests/transpiler/test_cmake_glue/ to exercise the helper from
// `cmake/SturmTranspile.cmake`.
//
// Deliberately minimal: no <sturm/sturm.hpp> include, no quantum ops.
// The goal is to validate the BUILD-SYSTEM glue, not the transpiler's
// quantum-AST rewriting (that is covered by M9's emitter tests and M11's
// fixture snapshot). A plain C++ program is sufficient because the
// transpiler's pipeline still runs: the matcher finds nothing, synthesize
// returns an empty insertion list, and the emitter writes the file out
// with an AUTO-GENERATED header prepended. That header is what the
// add_test greps for to confirm the generated file is the compile unit.
//
// The STURM_GLUE_FIXTURE_MARKER compile definition is threaded through
// target_compile_definitions so the test can additionally verify the
// body of the file is preserved through the transpile rewrite (the
// identifier must still appear in the generated .cpp).

int main() {
    // STURM_GLUE_FIXTURE_MARKER identifies this fixture in greps over the
    // generated artifact. Unique enough that accidental matches are
    // vanishingly unlikely.
    const int sturm_glue_fixture_marker = 0xA511;
    return sturm_glue_fixture_marker - 0xA511;
}
