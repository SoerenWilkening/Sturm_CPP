// test_no_cap_artifacts.cpp — sturm-zbzo (Frontend simpl. P2.a / G5).
// Plan §3 Phase 2 / PRD §5.5.
//
// Permanent regression gate: after the qubit-cap removal lands, the
// production trees (include/, src/, transpiler/src/) must contain no
// surviving cap-meaning tokens.
//
// Forbidden tokens (PRD A4 / plan §3 Phase 2):
//   * the legacy compile-time pool-cap knob.
//   * the legacy abort sentinel constant.
//   * the deleted abort message body fragment ("max 17").
//
// (The literal token spellings are assembled at run time below, via
// string concatenation, so this file itself does not contain the
// substrings the EXIT CRITERION grep would otherwise flag — sturm-5jta.)
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

// Forbidden patterns assembled from string fragments so this source
// file does not itself contain the substrings the EXIT CRITERION grep
// would otherwise flag (sturm-5jta).  The runtime payload spans:
//   * the legacy compile-time pool-cap macro identifier;
//   * the legacy abort sentinel constant identifier;
//   * the deleted abort message body fragment.
static const std::string& cap_macro() {
    static const std::string s =
        std::string("STURM_") + "ANCILLA_" + "CAPACITY";
    return s;
}
static const std::string& cap_sentinel() {
    static const std::string s = std::string("kCap") + "ExceededMsg";
    return s;
}
static const std::string& cap_msg_fragment() {
    static const std::string s = std::string("max") + " " + "17";
    return s;
}

static const std::string* forbidden_tokens() {
    static const std::string toks[] = {
        cap_macro(), cap_sentinel(), cap_msg_fragment(),
    };
    return toks;
}
static constexpr std::size_t kForbiddenCount = 3;

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
            const std::string* toks = forbidden_tokens();
            for (std::size_t i = 0; i < kForbiddenCount; ++i) {
                if (line.find(toks[i]) != std::string::npos) {
                    hits.push_back({entry.path().string(), lineno, toks[i]});
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
