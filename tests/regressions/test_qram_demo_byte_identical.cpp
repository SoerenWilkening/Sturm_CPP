// test_qram_demo_byte_identical.cpp — sturm-yggr (Frontend simpl. P8 /
// PRD A1).  Plan §3 Phase 8.
//
// Byte-identical regression gate.  After Phase 8 migrates
// `examples/qram_demo.cpp` to the new umbrella + opt-in-header shape
// (PRD §4 "After" listing), the rewritten demo's stdout must match the
// pre-rewrite baseline captured at the START of Phase 8 (R4
// mitigation: the baseline is captured AFTER every Phase 1–7
// implementation has landed but BEFORE the Phase 8 migration alters
// `qram_demo.cpp` itself, so any incidental ordering shifts that
// stem from the auto-injected lifecycle, the no-arg renderer, or the
// matcher pipeline are reflected in the recorded baseline).
//
// Mechanics
// ---------
// 1. `EXAMPLE_BINARY_PATH` is plumbed at configure time via
//    `target_compile_definitions` and points at the built
//    `example_qram_demo` executable.
// 2. `EXPECTED_OUTPUT_PATH` is plumbed in the same way and points at
//    the committed `tests/regressions/data/qram_demo_baseline.txt`
//    file.
// 3. We `popen()` the example and capture every byte of stdout,
//    then read the baseline file in full and `memcmp`.  On
//    mismatch we print a unified-style diff prefix (first 50 lines
//    each side) and exit non-zero so a CI run preserves enough
//    context for triage without turning the failure log into a
//    huge dump.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifndef EXAMPLE_BINARY_PATH
#error "EXAMPLE_BINARY_PATH must be set via target_compile_definitions"
#endif
#ifndef EXPECTED_OUTPUT_PATH
#error "EXPECTED_OUTPUT_PATH must be set via target_compile_definitions"
#endif

namespace {

// Read the entire stdout of `cmd` into a string. Returns true on
// success, false on any popen / read / pclose failure (with the
// captured exit status placed into `*exit_status`).
bool capture_stdout(const std::string& cmd,
                    std::string* out,
                    int* exit_status) {
    if (out == nullptr || exit_status == nullptr) return false;
    out->clear();
    std::FILE* p = popen(cmd.c_str(), "r");
    if (p == nullptr) return false;
    char buf[4096];
    while (true) {
        std::size_t n = std::fread(buf, 1, sizeof(buf), p);
        if (n == 0) break;
        out->append(buf, n);
    }
    const int rc = pclose(p);
    *exit_status = rc;
    return true;
}

// Read an entire file into a string. Returns true on success.
bool read_file(const std::string& path, std::string* out) {
    if (out == nullptr) return false;
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

// Print the first `max_lines` of `text` to stderr with a marker prefix
// per line so the output is easy to grep / triage.
void print_prefix_lines(const std::string& text,
                        const char* prefix,
                        std::size_t max_lines) {
    std::size_t lines_emitted = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            std::fprintf(stderr, "%s%.*s\n",
                         prefix,
                         static_cast<int>(i - start),
                         text.data() + start);
            ++lines_emitted;
            if (lines_emitted >= max_lines) {
                std::fprintf(stderr, "%s... (truncated)\n", prefix);
                return;
            }
            start = i + 1;
        }
    }
    if (start < text.size()) {
        const std::size_t tail = text.size() - start;
        std::fprintf(stderr, "%s%.*s\n",
                     prefix,
                     static_cast<int>(tail),
                     text.data() + start);
    }
}

} // namespace

int main() {
    const std::string binary = EXAMPLE_BINARY_PATH;
    const std::string baseline_path = EXPECTED_OUTPUT_PATH;

    std::string actual;
    int exit_status = 0;
    if (!capture_stdout(binary, &actual, &exit_status)) {
        std::fprintf(stderr,
                     "test_qram_demo_byte_identical: failed to capture "
                     "stdout from `%s`\n",
                     binary.c_str());
        return 1;
    }
    if (exit_status != 0) {
        std::fprintf(stderr,
                     "test_qram_demo_byte_identical: example exited "
                     "non-zero (status=%d)\n",
                     exit_status);
        return 1;
    }

    std::string expected;
    if (!read_file(baseline_path, &expected)) {
        std::fprintf(stderr,
                     "test_qram_demo_byte_identical: failed to read "
                     "baseline `%s`\n",
                     baseline_path.c_str());
        return 1;
    }

    if (actual == expected) {
        std::puts("test_qram_demo_byte_identical: PASS "
                  "(stdout matches baseline)");
        return 0;
    }

    std::fprintf(stderr,
                 "test_qram_demo_byte_identical: MISMATCH "
                 "(actual %zu bytes, expected %zu bytes)\n",
                 actual.size(), expected.size());
    std::fprintf(stderr, "── Expected (first 50 lines) ──\n");
    print_prefix_lines(expected, "[E] ", 50);
    std::fprintf(stderr, "── Actual (first 50 lines) ──\n");
    print_prefix_lines(actual, "[A] ", 50);
    return 1;
}
