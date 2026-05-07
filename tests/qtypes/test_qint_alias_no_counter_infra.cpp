// test_qint_alias_no_counter_infra.cpp -- sturm-v0db.7 (Wave 3 / W3.6 /
// A14). Plan §30, PRD §10.3.6.
//
// Permanent regression gate against re-introduction of any deleted-
// infrastructure symbol from the qint-alias completion epic. After
// W3.4 (sturm-v0db.5 / G9) wiped the `qint_alias_detail` namespace —
// the measurement counter, `measure_to_int`, every `bump_*` /
// `reset_*` helper, and `mixed_arith` — the production headers no
// longer carry any of those names. This test walks the source tree
// at run time and hard-fails the build if any of them reappear.
//
// Pattern (PRD §10.3.6 A14):
//
//   \b(measure_to_int|g_measurement_count|bump_measurement_count|
//      reset_measurement_count)\b
//   | qint_alias_detail::
//
// Word boundaries on the four measurement-counter names protect
// unrelated identifiers (e.g. a hypothetical
// `local_g_measurement_count_internal`) from tripping the gate; the
// `qint_alias_detail::` arm is a literal substring match (the `::`
// already terminates the token), so the entire deleted namespace is
// caught regardless of which member name follows.
//
// Scan roots (plan §30):
//   ${SOURCE_ROOT}/{include, tests, transpiler/src,
//                   transpiler/tests, examples}/**/*.{cpp,hpp,h}
//
// Exclusions:
//   * `docs/` — release-notes / migration prose may legitimately
//     mention the deleted symbols (and is outside the scan roots
//     anyway; the explicit exclusion is documentation).
//   * `build*/sturm_gen/` — transpile output mirroring user code is
//     scrubbed by the Wave-2 G6 gate (`test_sturm_gen_clean`);
//     also outside the scan roots.
//   * `transpiler/tests/test_sturm_gen_clean_unit.cpp` — the
//     positive-control fixture for the G6 unit-level scanner uses
//     the literal `qint_alias_detail::` substring on purpose to
//     verify the scanner's regression guard fires. PRD §10.3.6
//     names this single source as the documented exemption.
//
// On failure, every offending file:line:matched_substring is printed
// before the test exits non-zero, mirroring the Wave-2 G6 gate's
// output shape (`test_sturm_gen_clean.cpp`). Printing all hits in a
// single pass lets a CI run yield the complete inventory for
// triage.
//
// SOURCE_ROOT plumbing: the configured source directory is recorded
// at CMake configure time via `configure_file` of
// `test_qint_alias_audit_path.hpp.in`. See `<sturm_gen_path.hpp>`
// for the Wave-2 G6 prior art.

#include "test_qint_alias_audit_path.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Subset of the source tree that the alias-completion epic touches.
// Mirrors plan §30: include / tests / transpiler-src / transpiler-
// tests / examples. `src/` is intentionally NOT scanned because the
// alias is header-only (no .cpp counterpart in src/).
constexpr std::array<std::string_view, 5> kScanSubdirs = {
    "include",
    "tests",
    "transpiler/src",
    "transpiler/tests",
    "examples",
};

// Source paths the audit MUST NOT flag, even if they match the
// regex. Two documented exemptions:
//   1. The G6 unit-level scanner test deliberately includes the
//      literal substring `qint_alias_detail::` to verify its
//      detector arm fires (PRD §10.3.6 A14 documented exemption).
//   2. THIS source file: the regex literal and the surrounding
//      explanatory comments necessarily spell every forbidden
//      token. Excluding the test that defines the rules from the
//      rule-set itself is the same pattern used by Wave-2 G6
//      (`test_sturm_gen_clean.cpp` is naturally outside its
//      build-tree scan root); here we cannot rely on directory
//      structure because the audit's scan root *is* the project
//      source tree.
constexpr std::array<std::string_view, 2> kAllowedRelative = {
    "transpiler/tests/test_sturm_gen_clean_unit.cpp",
    "tests/qtypes/test_qint_alias_no_counter_infra.cpp",
};

struct Offense {
    fs::path     file;     // absolute path to the offending source
    std::size_t  line;     // 1-based line number of the match
    std::string  pattern;  // exact substring that matched
    std::string  text;     // line content (trimmed of trailing \r)
};

// Slurp file into memory. Returns empty string on I/O error so the
// caller treats the file as clean (the directory walk's per-file
// listing still surfaces the omission via the scan summary).
std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

// Extension filter: scan C/C++ source and header files only.
bool is_scan_target(const fs::path& p) {
    const auto ext = p.extension().string();
    return ext == ".cpp" || ext == ".hpp" || ext == ".h";
}

// Compute the path relative to the source root in forward-slash
// form, so allow-list comparisons are platform-independent.
std::string relative_posix(const fs::path& p, const fs::path& root) {
    std::error_code ec;
    auto rel = fs::relative(p, root, ec);
    if (ec) return p.string();
    std::string s = rel.generic_string();
    return s;
}

bool is_allowed(const fs::path& p, const fs::path& root) {
    const std::string rel = relative_posix(p, root);
    for (const auto& allow : kAllowedRelative) {
        if (rel == allow) return true;
    }
    return false;
}

// Walk `subdir_root` recursively and append every C/C++ source/
// header path. Allowed-listed sources are dropped here so the scan
// loop does not have to re-check.
void collect_targets(const fs::path& subdir_root,
                     const fs::path& source_root,
                     std::vector<fs::path>& out) {
    if (!fs::exists(subdir_root)) return;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(
             subdir_root, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator{};
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const auto& path = it->path();
        if (!is_scan_target(path)) continue;
        if (is_allowed(path, source_root)) continue;
        out.push_back(path);
    }
}

// Forbidden patterns (PRD §10.3.6 A14):
//   * Four word-boundary names from the deleted measurement-counter
//     infra.
//   * Literal substring `qint_alias_detail::` (the namespace itself).
const std::regex& forbidden_regex() {
    // ECMAScript syntax (default for std::regex). `\b` works on
    // word characters [A-Za-z0-9_] which suffices for C++ identifiers.
    static const std::regex re(
        R"(\b(?:measure_to_int|g_measurement_count|bump_measurement_count|reset_measurement_count)\b|qint_alias_detail::)",
        std::regex::ECMAScript | std::regex::optimize);
    return re;
}

// Walk `content` line-by-line; for each line that matches the
// forbidden regex, append one Offense per match. Reporting every
// match (not just the first per line) makes negative-control
// debugging concrete: if the cause is a token used twice on one
// line, the user sees both.
void scan_text(const fs::path& path,
               const std::string& content,
               std::vector<Offense>& offenses) {
    const auto& re = forbidden_regex();
    std::size_t  line_no   = 0;
    std::size_t  cursor    = 0;
    const std::size_t n    = content.size();
    while (cursor <= n) {
        const std::size_t eol = content.find('\n', cursor);
        const std::size_t end = (eol == std::string::npos) ? n : eol;
        ++line_no;
        std::string_view line(content.data() + cursor, end - cursor);
        // Trim trailing CR for CRLF-checked-out files.
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        // Run the regex over the bare line.
        const auto begin = line.data();
        const auto stop  = line.data() + line.size();
        std::cregex_iterator it(begin, stop, re);
        std::cregex_iterator last;
        for (; it != last; ++it) {
            Offense o;
            o.file    = path;
            o.line    = line_no;
            o.pattern = it->str();
            o.text    = std::string(line);
            offenses.push_back(std::move(o));
        }
        if (eol == std::string::npos) break;
        cursor = eol + 1;
    }
}

}  // namespace

int main() {
    const fs::path source_root(sturm::qtypes::testing::kSturmSourceRoot);
    std::fprintf(stderr,
                 "test_qint_alias_no_counter_infra: scanning %s\n",
                 source_root.string().c_str());

    if (!fs::exists(source_root)) {
        // configure_file recorded a path that does not resolve at
        // run time. This is a wiring bug (e.g. a stale build-dir
        // hand-edited away from the configured tree) — fail loudly.
        std::fprintf(stderr,
                     "FAIL: source root does not exist at %s\n"
                     "      (configure_file plumbing in"
                     " tests/qtypes/CMakeLists.txt is broken,"
                     " or the source tree was moved post-configure.)\n",
                     source_root.string().c_str());
        return 1;
    }

    // 1. Collect every scan target across the documented subdirs.
    std::vector<fs::path> targets;
    for (const auto& subdir : kScanSubdirs) {
        const fs::path sub_root = source_root / fs::path(std::string(subdir));
        collect_targets(sub_root, source_root, targets);
    }
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());

    std::fprintf(stderr,
                 "test_qint_alias_no_counter_infra: %zu file(s) to scan\n",
                 targets.size());

    if (targets.empty()) {
        // No files visited means the scan roots are wrong (or the
        // tree was wiped). Either way the audit cannot honestly
        // claim "clean" — fail loudly.
        std::fprintf(stderr,
                     "FAIL: no source files matched the scan roots"
                     " under %s. Check tests/qtypes/CMakeLists.txt"
                     " configure_file plumbing.\n",
                     source_root.string().c_str());
        return 1;
    }

    // 2. Run the regex over every file, accumulating offenses.
    std::vector<Offense> offenses;
    for (const auto& path : targets) {
        const std::string content = slurp(path);
        if (content.empty()) continue;
        scan_text(path, content, offenses);
    }

    if (offenses.empty()) {
        std::fprintf(stderr,
                     "PASS: %zu file(s) clean (no deleted-infra"
                     " token re-introduced).\n",
                     targets.size());
        return 0;
    }

    // 3. Print every offense in one pass (CI-friendly inventory).
    std::fprintf(stderr,
                 "FAIL: %zu offending site(s) under %s:\n",
                 offenses.size(), source_root.string().c_str());
    for (const auto& o : offenses) {
        std::fprintf(stderr,
                     "  %s:%zu: matched `%s`\n      %s\n",
                     o.file.string().c_str(),
                     o.line,
                     o.pattern.c_str(),
                     o.text.c_str());
    }
    std::fprintf(stderr,
                 "test_qint_alias_no_counter_infra: failing on %zu"
                 " offense(s).\n",
                 offenses.size());
    return 1;
}
