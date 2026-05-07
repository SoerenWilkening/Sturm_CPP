// Frontend simpl. P7 (sturm-e3ru): R3 mitigation — skip gtest sources.
//
// PRD §3 Non-goals: "GoogleTest / Catch fixtures continue to call
// sturm_backend_create / sturm_set_thread_context explicitly." gtest
// links its own `main` (when linked against `gtest_main`); a user-
// defined `int main(int, char**)` in a `test_*.cpp` file would
// otherwise be eligible for our auto-injection and collide with
// gtest's own driver.
//
// The matcher mitigates R3 by inspecting the source-file name: any
// FunctionDecl whose containing file matches the substring `gtest`
// is left alone. This fixture's name contains `gtest` precisely to
// trigger that skip — the expected output is byte-identical to the
// input (modulo the auto-generated header).
//
// Note: this fixture defines BOTH the umbrella sentinel (so the
// matcher gets past gates 1-5) AND has `gtest` in its filename — the
// only remaining gate that fires is the R3 source-file probe.
#define STURM_UMBRELLA_INCLUDED 1

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    return 0;
}
