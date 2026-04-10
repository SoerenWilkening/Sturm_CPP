// test_no_v2_namespace.cpp — M20 cleanup guard.
//
// Grep-based check: no file outside tests/ uses the sturm::v2:: namespace.
// Passes if the search finds zero matches.
//
// Harness: plain assert + main (no gtest).

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifndef STURM_REPO_ROOT
#  error "STURM_REPO_ROOT must be defined via -DSTURM_REPO_ROOT=... in CMakeLists.txt"
#endif

int main() {
    // Search include/ and src/ for any sturm::v2:: reference.
    std::string cmd =
        "grep -r --include='*.hpp' --include='*.h' --include='*.cpp' --include='*.c'"
        " 'sturm::v2::'"
        " " STURM_REPO_ROOT "/include"
        " " STURM_REPO_ROOT "/src"
        " 2>/dev/null | wc -l";

    FILE* pipe = popen(cmd.c_str(), "r");
    assert(pipe && "popen failed");

    char buf[64] = {};
    if (fgets(buf, sizeof(buf), pipe)) {
        /* consume */
    }
    pclose(pipe);

    int count = std::atoi(buf);
    if (count != 0) {
        std::fprintf(stderr,
            "FAIL: %d file(s) outside tests/ still use sturm::v2:: namespace\n",
            count);
        // Print the offending lines for diagnosis.
        std::string show_cmd =
            "grep -r --include='*.hpp' --include='*.h' --include='*.cpp' --include='*.c'"
            " 'sturm::v2::'"
            " " STURM_REPO_ROOT "/include"
            " " STURM_REPO_ROOT "/src"
            " 2>/dev/null";
        system(show_cmd.c_str());
    }
    assert(count == 0 && "sturm::v2:: namespace found outside tests/");

    std::printf("test_no_v2_namespace: PASS (0 sturm::v2:: refs in include/ and src/)\n");
    return 0;
}
