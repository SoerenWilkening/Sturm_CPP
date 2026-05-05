// test_sturm_gen_clean_unit.cpp -- sturm-8a2q.
//
// Unit-level coverage for `test_sturm_gen_clean_scan.hpp`. Drives the
// pure-text scan over hand-crafted in-memory fixtures (no filesystem
// walk) so we can pin both directions:
//   * positive-control: regression mode that motivated sturm-7t85
//     (`qint a[4];` + `using sturm::qint;` carrier import) MUST trip
//     the gate.
//   * negative-control: a representative slice of the currently-clean
//     sturm_gen output (qualified `qint_t<W>`, comments referencing
//     bare `qint`, the benign `using sturm::qint;` import line, an
//     `#include "sturm/qtypes/qint.hpp"`, and identifier suffixes
//     such as `uncompute_eq_qint`) MUST stay clean.

#include "test_sturm_gen_clean_scan.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

using sturm::transpile::testing::Offense;
using sturm::transpile::testing::scan_text;

bool expect_offense(std::string_view label,
                    std::string_view content,
                    std::size_t expected_line,
                    std::string_view expected_pat_prefix) {
    std::vector<Offense> hits;
    scan_text(fs::path{"<inline>"}, content, hits);
    for (const auto& h : hits) {
        if (h.line == expected_line &&
            h.pat.substr(0, expected_pat_prefix.size()) ==
                expected_pat_prefix) {
            return true;
        }
    }
    std::fprintf(stderr,
                 "FAIL[%.*s]: expected hit at line %zu matching `%.*s`,"
                 " got %zu offenses:\n",
                 static_cast<int>(label.size()), label.data(),
                 expected_line,
                 static_cast<int>(expected_pat_prefix.size()),
                 expected_pat_prefix.data(),
                 hits.size());
    for (const auto& h : hits) {
        std::fprintf(stderr, "    line %zu pat=`%.*s` text=`%s`\n",
                     h.line,
                     static_cast<int>(h.pat.size()), h.pat.data(),
                     h.text.c_str());
    }
    return false;
}

bool expect_clean(std::string_view label, std::string_view content) {
    std::vector<Offense> hits;
    scan_text(fs::path{"<inline>"}, content, hits);
    if (hits.empty()) return true;
    std::fprintf(stderr,
                 "FAIL[%.*s]: expected zero offenses, got %zu:\n",
                 static_cast<int>(label.size()), label.data(),
                 hits.size());
    for (const auto& h : hits) {
        std::fprintf(stderr, "    line %zu pat=`%.*s` text=`%s`\n",
                     h.line,
                     static_cast<int>(h.pat.size()), h.pat.data(),
                     h.text.c_str());
    }
    return false;
}

}  // namespace

int main() {
    int failures = 0;

    // ── Positive-control: wave-2 array-element matcher regression ──
    //    Mirrors the line shape that survives in `sturm_gen/examples/
    //    qram_demo.cpp` if the array-element arm in
    //    `qint_alias_carrier_walk.cpp` is commented out.
    const std::string regress =
        "using sturm::qint;\n"
        "int main() {\n"
        "    qint a[4];\n"
        "    qint i = 10;\n"
        "    qint b = a[i];\n"
        "}\n";
    if (!expect_offense("array-element residue",  regress, 3,
                        "bare-spelling")) ++failures;
    if (!expect_offense("scalar residue",          regress, 4,
                        "bare-spelling")) ++failures;
    if (!expect_offense("init-from-subscript",     regress, 5,
                        "bare-spelling")) ++failures;

    // Pointer carrier (parity with the matcher's pointer arm).
    const std::string regress_ptr =
        "using sturm::qint;\n"
        "void f() { qint *p; }\n";
    if (!expect_offense("pointer residue", regress_ptr, 2,
                        "bare-spelling")) ++failures;

    // Qualified pattern still trips (G4 baseline).
    const std::string regress_qual =
        "sturm::frontend::qint x;\n";
    if (!expect_offense("qualified residue", regress_qual, 1,
                        "sturm::frontend::qint")) ++failures;

    // ── Negative-controls: representative clean idioms ──
    if (!expect_clean("qualified qint_t",
                      "sturm::qint_t<4> b;\n")) ++failures;
    if (!expect_clean("bare qint in line comment",
                      "// rewrites `qint b = a[i];` before codegen\n"
                      "sturm::qint_t<4> b;\n")) ++failures;
    if (!expect_clean("bare qint in string literal",
                      "const char* s = \"qint b = a[i];\";\n")) ++failures;
    if (!expect_clean("benign using sturm::qint import",
                      "using sturm::qint;\n"
                      "sturm::qint_t<4> b;\n")) ++failures;
    if (!expect_clean("include header containing qint",
                      "#include \"sturm/qtypes/qint.hpp\"\n")) ++failures;
    if (!expect_clean("identifier suffix uncompute_*_qint",
                      "uncompute_eq_qint(c, a, b);\n")) ++failures;
    if (!expect_clean("identifier prefix qint_alias_detail",
                      "using sturm::frontend::qint_alias_detail::"
                      "measurement_count;\n")) ++failures;
    if (!expect_clean("block comment spanning lines",
                      "/* qint a[4]; legacy spelling -- documented\n"
                      "   for migration purposes */\n"
                      "sturm::qint_t<4> b;\n")) ++failures;

    if (failures != 0) {
        std::fprintf(stderr,
                     "test_sturm_gen_clean_unit: %d failure(s).\n",
                     failures);
        return 1;
    }
    std::fprintf(stderr,
                 "test_sturm_gen_clean_unit: all checks passed.\n");
    return 0;
}
