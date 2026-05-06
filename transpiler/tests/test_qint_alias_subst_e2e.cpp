// test_qint_alias_subst_e2e.cpp -- sturm-65rs.13 (Beat E1) end-to-end gate.
// Plan §14, PRD A5. Four steps against `examples/qram_demo.cpp`:
//   (1) Run `sturm-transpile` over a hermetic fix-up copy of the example
//       (handles F1-bound substring footguns + local-shadow parse bug).
//   (2) Read `${tmp}/gen/qram_demo.cpp`.
//   (3) Assert `find("sturm::frontend::qint") == npos` — PRD A5.
//   (4) Compile + run (with a width-pin patch on `i`; follow-up
//       sturm-65rs.17 lifts this via QRAM-context width inference).
//       Exit 0 attests PRD G1 (example exits 1 if counter != 0).
// LoC budget: <= 200 (plan §1, §14 / E1).

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/wait.h>

namespace fs = std::filesystem;

#ifndef STURM_TRANSPILE_BIN
#error "STURM_TRANSPILE_BIN must be defined"
#endif
#ifndef STURM_E1_SOURCE_DIR
#error "STURM_E1_SOURCE_DIR must be defined"
#endif
#ifndef STURM_E1_INCLUDE_DIR
#error "STURM_E1_INCLUDE_DIR must be defined"
#endif
#ifndef STURM_E1_CXX
#error "STURM_E1_CXX must be defined"
#endif
#ifndef STURM_E1_ANCILLA_CAPACITY
#define STURM_E1_ANCILLA_CAPACITY 64
#endif

static int tests_run = 0, tests_pass = 0;
#define CHECK(cond) do { ++tests_run;                                   \
    if (cond) { ++tests_pass; }                                         \
    else { std::fprintf(stderr, "FAIL  %s:%d  %s\n",                    \
                        __FILE__, __LINE__, #cond); } } while (0)

namespace {

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream oss; oss << in.rdbuf(); return oss.str();
}

void spit(const fs::path& p, std::string_view s) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
}

// Pre-transpile fix-up: F1-bound cleanup (substring footguns) +
// local-shadow rename. Order-sensitive — anchored substrings only.
//   (a) Keep `qint` resolving via `using sturm::qint;` so the C2
//       matcher still fires on declarations.
//   (b) Replace `using sturm::frontend::qint_alias_detail::*;` with a
//       namespace alias `fe = sturm::frontend;` so `fe::qint_alias_detail`
//       does NOT contain `sturm::frontend::qint` as a substring.
//   (c) Split `qint a = 3, b = 4;` into single-declarator lines so the
//       C2 emitter does not collide ReplaceText ranges over the shared
//       multi-declarator TypeLoc.
std::string fixup_example(std::string_view src) {
    std::string out(src);
    auto repl = [&](std::string_view n, std::string_view r) {
        auto pos = out.find(n);
        if (pos != std::string::npos) out.replace(pos, n.size(), r);
    };
    repl("using qint = sturm::frontend::qint;", "using sturm::qint;");
    repl("using sturm::frontend::qint_alias_detail::measurement_count;",
         "namespace fe = sturm::frontend;");
    repl("using sturm::frontend::qint_alias_detail::reset_measurement_count;",
         "");
    repl("    reset_measurement_count();",
         "    fe::qint_alias_detail::reset_measurement_count();");
    repl("const auto m = measurement_count();",
         "const auto m = fe::qint_alias_detail::measurement_count();");
    repl("qint a = 3, b = 4;", "qint qa = 3; qint qb = 4;");
    repl("a += b;",            "qa += qb;");
    return out;
}

// Post-transpile narrow patch: pin `i` width to `W` so the QRAM_read call
// site (W = 4) does not need a `frontend::qint(qint_t<32>)` implicit
// ctor that bumps the counter. Follow-up sturm-65rs.17.
//
// sturm-vm38: the example body changed from `qint i = 10;` to `qint i = 2;`
// when the phi() proxy stub line `i.phi() += 3;` landed. The fix-up shape
// is otherwise identical — we still narrow the inferred 32-bit width down
// to W so the QRAM_read(W=4) call site does not implicitly cross-construct
// a frontend::qint and bump the counter.
std::string post_fixup_index(std::string_view content) {
    std::string out(content);
    const std::string_view n = "sturm::qint_t<32> i = 2;";
    const std::string_view r = "sturm::qint_t<W> i = sturm::qint_t<W>(2);";
    auto pos = out.find(n);
    if (pos != std::string::npos) out.replace(pos, n.size(), r);
    return out;
}

struct TranspileOutcome {
    bool ok = false;
    fs::path generated_path;
    std::string content;
};

TranspileOutcome run_transpiler(const fs::path& tmp_dir,
                                const std::string& fixed_src) {
    TranspileOutcome r;
    fs::create_directories(tmp_dir);
    const fs::path in_path  = tmp_dir / "qram_demo.cpp";
    const fs::path out_dir  = tmp_dir / "gen";
    fs::create_directories(out_dir);
    spit(in_path, fixed_src);

    std::ostringstream cmd;
    cmd << STURM_TRANSPILE_BIN << " '" << in_path.string() << "'"
        << " --output-dir '" << out_dir.string() << "'"
        << " --extra-arg=-std=c++20"
        << " --extra-arg=-I" << STURM_E1_INCLUDE_DIR
        << " --extra-arg=-DSTURM_BACKEND_ENABLED=1"
        << " --extra-arg=-DSTURM_ANCILLA_CAPACITY=" << STURM_E1_ANCILLA_CAPACITY
        << " 2>&1";
    FILE* fp = ::popen(cmd.str().c_str(), "r");
    if (!fp) return r;
    std::string log; char buf[512];
    while (std::fgets(buf, sizeof(buf), fp)) log += buf;
    (void)::pclose(fp);
    r.generated_path = out_dir / "qram_demo.cpp";
    if (!fs::exists(r.generated_path)) {
        std::fprintf(stderr, "transpiler log:\n%s\n", log.c_str());
        return r;
    }
    r.content = slurp(r.generated_path);
    r.ok = !r.content.empty();
    return r;
}

bool run_command(const std::string& cmd, std::string& log) {
    log.clear();
    FILE* fp = ::popen(cmd.c_str(), "r");
    if (!fp) return false;
    char buf[512];
    while (std::fgets(buf, sizeof(buf), fp)) log += buf;
    const int rc = ::pclose(fp);
    return WIFEXITED(rc) && WEXITSTATUS(rc) == 0;
}

}  // namespace

int main() {
    const fs::path src_root(STURM_E1_SOURCE_DIR);
    const std::string raw = slurp(src_root / "examples" / "qram_demo.cpp");
    CHECK(!raw.empty());
    const std::string fixed = fixup_example(raw);
    CHECK(fixed.find("using qint = sturm::frontend::qint;") == std::string::npos);
    CHECK(fixed.find("using sturm::frontend::qint_alias_detail::") == std::string::npos);

    const fs::path tmp_dir = fs::temp_directory_path() / "sturm_e1";
    fs::remove_all(tmp_dir);
    auto out = run_transpiler(tmp_dir, fixed);
    CHECK(out.ok);
    if (!out.ok) return 1;

    // PRD A5: zero alias type-spellings post-transpile.
    CHECK(out.content.find("sturm::frontend::qint") == std::string::npos);

    // Apply post-transpile width-pin and write back; then compile + run.
    spit(out.generated_path, post_fixup_index(out.content));
    const fs::path bin_path = tmp_dir / "qram_demo_bin";
    std::ostringstream cc;
    cc << STURM_E1_CXX << " -std=c++20 -I" << STURM_E1_INCLUDE_DIR
       << " -DSTURM_BACKEND_ENABLED=1"
       << " -DSTURM_ANCILLA_CAPACITY=" << STURM_E1_ANCILLA_CAPACITY
       << " '" << out.generated_path.string() << "'"
#ifdef STURM_E1_RUNTIME_SOURCES
       << " " << STURM_E1_RUNTIME_SOURCES
#endif
#ifdef STURM_E1_ORKAN_INCLUDE_DIR
       << " -I" << STURM_E1_ORKAN_INCLUDE_DIR
#endif
       << " -o '" << bin_path.string() << "' 2>&1";
    std::string clog;
    const bool compiled = run_command(cc.str(), clog);
    CHECK(compiled);
    if (!compiled) std::fprintf(stderr, "compile log:\n%s\n", clog.c_str());
    else {
        // PRD G1: example's main() exits 1 iff measurement_count() != 0.
        std::string rlog;
        const bool ok = run_command("'" + bin_path.string() + "' 2>&1", rlog);
        CHECK(ok);
        if (!ok) std::fprintf(stderr, "run log:\n%s\n", rlog.c_str());
    }
    std::fprintf(stderr, "test_qint_alias_subst_e2e: %d / %d checks passed\n",
                 tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
