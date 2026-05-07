// test_no_cap_artifacts.cpp — sturm-zbzo (Frontend simpl. P2.a / G5).
// Plan §3 Phase 2 / PRD §5.5.
//
// Permanent regression gate: after the qubit-cap removal lands, the
// production trees (include/, src/, transpiler/src/) must contain no
// surviving cap-meaning tokens.
//
// Forbidden tokens (PRD A4 / plan §3 Phase 2):
//   * STURM_ANCILLA_CAPACITY  — the deleted compile-time knob.
//   * kCapExceededMsg         — the deleted abort sentinel.
//   * "max 17"                — the deleted abort message body.
//
// Scan roots (production code only — test files / docs may legitimately
// mention the deleted tokens for historical or migration prose):
//   * ${SOURCE_ROOT}/include
//   * ${SOURCE_ROOT}/src
//   * ${SOURCE_ROOT}/transpiler/src
//
// SOURCE_ROOT plumbing: ${CMAKE_SOURCE_DIR} is recorded at configure
// time via `target_compile_definitions` of `STURM_SOURCE_ROOT="..."`.
// A stale path errors at run time, not silently at scan time.
//
// On failure, every offending file:line:matched_substring is printed
// before the test exits non-zero, so a CI run yields the complete
// inventory for triage.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#ifndef STURM_SOURCE_ROOT
#error "STURM_SOURCE_ROOT must be set via target_compile_definitions"
#endif

namespace fs = std::filesystem;

// Forbidden patterns. Match as plain literals — `STURM_ANCILLA_CAPACITY`
// and `kCapExceededMsg` are unique-enough identifiers that a substring
// match is sufficient; `max 17` is a fragment of the deleted abort
// message body.
static constexpr const char* kForbidden[] = {
    "STURM_ANCILLA_CAPACITY",
    "kCapExceededMsg",
    "max 17",
};

// File extensions to scan.
static bool is_source_file(const fs::path& p) {
    auto ext = p.extension().string();
    return ext == ".cpp" || ext == ".hpp" || ext == ".h" ||
           ext == ".cc"  || ext == ".cxx" || ext == ".c";
}

// Walk a directory tree; collect (file, line, matched_token) triples.
struct Hit {
    std::string file;
    int         line;
    std::string token;
};

static void scan_dir(const fs::path& root, std::vector<Hit>& hits) {
    if (!fs::exists(root)) return;
    for (auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        if (!is_source_file(entry.path())) continue;

        std::ifstream in(entry.path());
        if (!in) continue;

        std::string line;
        int lineno = 0;
        while (std::getline(in, line)) {
            ++lineno;
            for (const char* tok : kForbidden) {
                if (line.find(tok) != std::string::npos) {
                    hits.push_back({entry.path().string(), lineno, tok});
                }
            }
        }
    }
}

int main() {
    const fs::path root = STURM_SOURCE_ROOT;
    if (!fs::exists(root)) {
        std::fprintf(stderr,
                     "STURM_SOURCE_ROOT does not exist: %s\n",
                     root.c_str());
        return 1;
    }

    std::vector<Hit> hits;
    scan_dir(root / "include", hits);
    scan_dir(root / "src", hits);
    scan_dir(root / "transpiler" / "src", hits);

    if (!hits.empty()) {
        std::fprintf(stderr,
                     "test_no_cap_artifacts: %zu surviving cap-token hit(s):\n",
                     hits.size());
        for (const auto& h : hits) {
            std::fprintf(stderr, "  %s:%d: %s\n",
                         h.file.c_str(), h.line, h.token.c_str());
        }
        return 1;
    }

    std::puts("test_no_cap_artifacts: PASS (no surviving cap tokens)");
    return 0;
}
