// test_qint_alias_subst_e2e.cpp -- sturm-65rs.13 (Beat E1) end-to-end gate.
// Plan §14, PRD A5. Four steps against `examples/qram_demo.cpp`:
//   (1) Run `sturm-transpile` over a hermetic fix-up copy of the example
//       (sturm-v0db.2 / W3.1: the fixup is now a no-op — the substring
//       footguns it patched are gone from the source).
//   (2) Read `${tmp}/gen/qram_demo.cpp`.
//   (3) Assert `find("sturm::frontend::qint") == npos` — PRD A5.
//   (4) Compile + run (with a width-pin patch on `i`; follow-up
//       sturm-65rs.17 lifts this via QRAM-context width inference).
//       Wave 3 (sturm-v0db.2 / W3.1): the run gate is exit-zero only.
//       The Wave-1 PRD G1 contract is replaced by Wave-2 G6
//       (`test_sturm_gen_clean` — pre-transpile-side coverage check;
//       counter infrastructure being deleted in W3.4 / G9).
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
// sturm-zva0: pre-formatted `-DSTURM_MODE_DEFAULT=STURM_MODE_<X>` flag string.
// Threaded from `sturm::frontend`'s INTERFACE_COMPILE_DEFINITIONS at configure
// time so the rewritten qram_demo.cpp's auto-injected
// `sturm_backend_create(STURM_MODE_DEFAULT)` (sturm-e3ru / P7) resolves
// when the test's host clang++ invocation compiles it back to a binary.
#ifndef STURM_E1_MODE_DEFAULT_FLAG
#error "STURM_E1_MODE_DEFAULT_FLAG must be defined to the `-DSTURM_MODE_DEFAULT=STURM_MODE_<X>` compile flag string"
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
//
// sturm-v0db.2 / W3.1: re-pinned against the current `examples/qram_demo.cpp`
// (PRD §10 / Wave 3 — pre-transpile execution unsupported). The
// Wave-1 `using sturm::frontend::detail-ns::*;` and `qint a = 3, b = 4;`
// shapes no longer appear in the example; the `using sturm::qint;`
// form already resolves the bare `qint` to the alias class via
// qint_fwd.hpp (post-B1), so no `using` rewrite is needed. The
// fixup_example function is retained as the central hook so future
// shape drift can be patched here without touching the run/compile
// harness; it is currently a no-op.
std::string fixup_example(std::string_view src) {
    std::string out(src);
    // Intentionally empty: examples/qram_demo.cpp post-W3 needs no
    // pre-transpile substring rewrites. Reserved for future drift.
    return out;
}

// Post-transpile narrow patch: historically pinned `i` width to a
// file-scope `W` (the `constexpr std::size_t W = 4;` that lived in the
// pre-P8 anonymous namespace of `examples/qram_demo.cpp`) so the
// QRAM_read call site did not need a `frontend::qint(qint_t<32>)`
// implicit ctor.
//
// sturm-o1zj: P8 (sturm-yggr) removed the file-scope `W` constant from
// the example as part of the frontend-simplification migration, and
// the rewritten array (`qint a[4]`) lands as `sturm::qint_t<32>[4]`,
// so all three QRAM_read arguments (`a`, `i`, `b`) already share
// width 32 in the rewritten TU. The historical narrow-patch is
// therefore a no-op (and would actually break compilation by
// reintroducing an undeclared `W`). The function is kept as the
// central hook so future shape drift can be patched here without
// touching the run/compile harness; it is currently a no-op.
std::string post_fixup_index(std::string_view content) {
    return std::string(content);
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
    // sturm-v0db.2 / W3.1 — re-pinned post-fixup-no-op.
    // The Wave-1 `using qint = sturm::frontend::qint;` shape no longer
    // appears in the source (`using sturm::qint;` form is fine post-B1).
    // Pin as ABSENT so any reintroduction fails loudly. The Wave-1
    // `using sturm::frontend::<detail-ns>::*;` shape is gated by W3.6's
    // tree-grep audit (the namespace is deleted under W3.4 / G9).
    CHECK(fixed.find("using qint = sturm::frontend::qint;") == std::string::npos);

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
       // sturm-zva0: forward STURM_MODE_DEFAULT=STURM_MODE_<X> from
       // `sturm::frontend` so the auto-injected lifecycle resolves.
       << " " << STURM_E1_MODE_DEFAULT_FLAG
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
        // sturm-v0db.2 / W3.1 — example's main() returns 0 on success.
        // The Wave-1 PRD G1 contract ("counter == 0 post-transpile")
        // has been replaced by the Wave-2 G6 sturm_gen-clean gate
        // (PRD §10 / W3 — counter infrastructure being deleted in
        // W3.4 / G9). The exit-zero check below remains the smoke
        // gate that the rewritten + recompiled program runs without
        // crashing or aborting.
        std::string rlog;
        const bool ok = run_command("'" + bin_path.string() + "' 2>&1", rlog);
        CHECK(ok);
        if (!ok) std::fprintf(stderr, "run log:\n%s\n", rlog.c_str());
    }
    std::fprintf(stderr, "test_qint_alias_subst_e2e: %d / %d checks passed\n",
                 tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
