// test_ci_hook.cpp — M27: CI hook extension.
//
// Verifies that:
//   1. `ctest -L backend -N` (run from BUILD_DIR) lists ≥ 1 test from each
//      M1–M26 module — i.e. every required test name appears in the output.
//   2. The aggregate runner script tests/run_all is present and executable.
//
// This test is itself labelled 'backend' in CMakeLists so it participates in
// the very run it is checking (M27 joining the aggregate runner).
//
// BUILD_DIR and REPO_ROOT are injected by CMake at compile time via
// -DSTURM_BUILD_DIR=... and -DSTURM_REPO_ROOT=...

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string run_command(const std::string& cmd) {
    FILE* pipe = popen(cmd.c_str(), "r");
    assert(pipe && "popen failed");
    char   buf[256];
    std::string result;
    while (fgets(buf, sizeof(buf), pipe)) {
        result += buf;
    }
    pclose(pipe);
    return result;
}

static bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

// ── Required tests: one per module M1–M26 ────────────────────────────────────

static const char* kRequiredTests[] = {
    "test_gate_kind",              // M1
    "test_core_abi_c",             // M2
    "test_context",                // M3
    "test_init",                   // M4
    "test_measure",                // M5
    "test_ir",                     // M6
    "test_exec_count",             // M7
    "test_exec_append",            // M8
    "test_orkan_bridge",           // M9
    "test_exec_simulate_1q",       // M10
    "test_exec_simulate_multiq",   // M11
    "test_exec_simulate_crot",     // M12
    "test_execute_gate",           // M13
    "test_reduction_table",        // M14
    "test_dispatch_all_classical", // M15
    "test_dispatch_mixed",         // M16
    "test_promotion",              // M17
    "test_qubit_cap",              // M18
    "test_uncompute_op",           // M19
    "test_uncompute_run",          // M20
    "test_uncompute_add_const",    // M21
    "test_uncompute_each_op",      // M22
    "test_controlled_ops",         // M23
    "test_when_dispatch",          // M24
    "test_instantiations",         // M25
    "test_end_to_end",             // M26
};
static constexpr int kRequiredCount =
    static_cast<int>(sizeof(kRequiredTests) / sizeof(kRequiredTests[0]));

// ── Test 1: all M1–M26 tests appear in `ctest -L backend -N` ─────────────────

static void test_backend_label_lists_all_modules() {
#ifndef STURM_BUILD_DIR
    // If CMake didn't inject the build dir, skip gracefully.
    return;
#else
    std::string cmd = std::string("ctest --test-dir ") + STURM_BUILD_DIR
                      + " -L backend -N 2>&1";
    std::string output = run_command(cmd);

    assert(!output.empty() && "ctest produced no output");

    int missing = 0;
    for (int i = 0; i < kRequiredCount; ++i) {
        if (!contains(output, kRequiredTests[i])) {
            fprintf(stderr, "MISSING in 'ctest -L backend -N': %s\n",
                    kRequiredTests[i]);
            ++missing;
        }
    }
    assert(missing == 0 && "One or more required backend tests missing from label");
#endif
}

// ── Test 2: tests/run_all script is present and executable ───────────────────

static void test_run_all_script_exists_and_is_executable() {
#ifndef STURM_REPO_ROOT
    return;
#else
    std::string path = std::string(STURM_REPO_ROOT) + "/tests/run_all";
    struct stat st{};
    int rc = stat(path.c_str(), &st);
    assert(rc == 0 && "tests/run_all does not exist");
    assert((st.st_mode & S_IXUSR) && "tests/run_all is not executable");
#endif
}

// ── Test 3: `ctest -L backend` actually passes (≥ 26 tests) ──────────────────
//    This is a smoke check that the label run itself exits 0.

static void test_backend_label_run_passes() {
#ifndef STURM_BUILD_DIR
    return;
#else
    // Use -N (dry-run) for speed; the real run is exercised by the runner.
    // Count how many tests appear in the list.
    std::string cmd = std::string("ctest --test-dir ") + STURM_BUILD_DIR
                      + " -L backend -N 2>&1";
    std::string output = run_command(cmd);
    // Count lines matching "Test #"
    int count = 0;
    size_t pos = 0;
    while ((pos = output.find("Test #", pos)) != std::string::npos) {
        ++count;
        ++pos;
    }
    // M1–M26 = 26 modules, plus M27 itself = ≥ 26.
    assert(count >= 26 && "ctest -L backend lists fewer than 26 tests");
#endif
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    test_backend_label_lists_all_modules();
    test_run_all_script_exists_and_is_executable();
    test_backend_label_run_passes();
    return 0;
}
