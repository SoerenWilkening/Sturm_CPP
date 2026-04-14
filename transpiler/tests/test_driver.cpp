// test_driver.cpp — end-to-end tests for the sturm-transpile binary (M4)
//
// These are integration tests: they spawn the freshly built binary and
// check its observable behavior:
//   1. `sturm-transpile --version` prints a version line and exits 0.
//   2. `sturm-transpile <hello.cpp> --output-dir <tmp>` produces a
//      byte-identical copy at <tmp>/hello.cpp (identity pass — M4 does
//      no rewriting yet).
//   3. A missing input file causes a non-zero exit.
//
// The CMake wiring passes the absolute path to the compiled binary as a
// preprocessor macro STURM_TRANSPILE_BIN. We do not hard-code a path; the
// test must work regardless of build-tree layout.

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

#ifndef STURM_TRANSPILE_BIN
#  error "STURM_TRANSPILE_BIN must be defined by the CMake build (path to the sturm-transpile binary)."
#endif

// ── Test harness ──────────────────────────────────────────────────────────────
static int tests_run  = 0;
static int tests_pass = 0;

#define CHECK(cond) do {                                              \
    ++tests_run;                                                      \
    if (cond) { ++tests_pass; }                                       \
    else {                                                            \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                     \
                     __FILE__, __LINE__, #cond);                      \
    }                                                                 \
} while (0)

// Run a shell command, return (exit_code, stdout+stderr).
static std::pair<int, std::string> run_cmd(const std::string& cmd) {
    std::string full = cmd + " 2>&1";
    FILE* pipe = ::popen(full.c_str(), "r");
    if (!pipe) return {-1, ""};
    std::string out;
    char buf[512];
    while (std::fgets(buf, sizeof(buf), pipe)) out += buf;
    int rc = ::pclose(pipe);
    // WEXITSTATUS is the right accessor, but plain rc is fine for
    // "zero vs non-zero" checks; we only inspect the sign below.
    if (WIFEXITED(rc)) return {WEXITSTATUS(rc), out};
    return {rc, out};
}

static std::string read_file_contents(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

static fs::path make_scratch_dir(const char* tag) {
    fs::path base = fs::temp_directory_path() / "sturm-m4-driver-tests";
    fs::create_directories(base);
    fs::path dir = base / tag;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

static fs::path quote(const fs::path& p) {
    // shell-quote a path by wrapping it in single quotes. The paths we use
    // are all under /tmp and never contain single quotes, so naïve wrapping
    // is safe here.
    return fs::path("'" + p.string() + "'");
}

// ── Tests ─────────────────────────────────────────────────────────────────────

static void test_version_flag_exits_zero() {
    std::string bin = STURM_TRANSPILE_BIN;
    auto [rc, out] = run_cmd(bin + " --version");
    CHECK(rc == 0);
    // Must mention "sturm-transpile" somewhere so users know which tool
    // they're querying.
    CHECK(out.find("sturm-transpile") != std::string::npos);
}

static void test_identity_copy() {
    fs::path dir = make_scratch_dir("identity");
    fs::path input  = dir / "hello.cpp";
    fs::path outdir = dir / "gen";
    fs::create_directories(outdir);

    const std::string payload =
        "// a tiny hello world — no quantum types, just plain C++.\n"
        "int main() { return 0; }\n";
    {
        std::ofstream o(input, std::ios::binary);
        o << payload;
    }

    std::string cmd = std::string(STURM_TRANSPILE_BIN) +
        " " + quote(input).string() +
        " --output-dir " + quote(outdir).string();
    auto [rc, out] = run_cmd(cmd);
    if (rc != 0) std::fprintf(stderr, "driver stderr:\n%s\n", out.c_str());
    CHECK(rc == 0);

    // Identity pass: exact byte copy of the input should land at
    // <outdir>/hello.cpp.
    fs::path expected = outdir / "hello.cpp";
    CHECK(fs::exists(expected));
    CHECK(read_file_contents(expected) == payload);
}

static void test_missing_input_exits_nonzero() {
    fs::path dir = make_scratch_dir("missing");
    fs::path input  = dir / "does_not_exist.cpp";
    fs::path outdir = dir / "gen";
    fs::create_directories(outdir);

    std::string cmd = std::string(STURM_TRANSPILE_BIN) +
        " " + quote(input).string() +
        " --output-dir " + quote(outdir).string();
    auto [rc, out] = run_cmd(cmd);
    CHECK(rc != 0);
}

static void test_nested_relative_input_mirrors_tree() {
    // Mirroring the source subtree under the output dir is part of the
    // driver contract (resolve_output_path). Cover it end-to-end.
    fs::path dir = make_scratch_dir("nested");
    fs::path sub = dir / "src" / "sub";
    fs::create_directories(sub);
    fs::path input = sub / "nested.cpp";
    const std::string payload = "int f() { return 42; }\n";
    {
        std::ofstream o(input, std::ios::binary);
        o << payload;
    }
    fs::path outdir = dir / "gen";
    fs::create_directories(outdir);

    // Run from `dir` so the input can be referenced as a relative path.
    std::string cmd =
        "cd " + quote(dir).string() + " && " +
        std::string(STURM_TRANSPILE_BIN) +
        " src/sub/nested.cpp " +
        "--output-dir " + quote(outdir).string();
    auto [rc, out] = run_cmd(cmd);
    if (rc != 0) std::fprintf(stderr, "driver stderr:\n%s\n", out.c_str());
    CHECK(rc == 0);

    fs::path expected = outdir / "src" / "sub" / "nested.cpp";
    CHECK(fs::exists(expected));
    CHECK(read_file_contents(expected) == payload);
}

int main() {
    test_version_flag_exits_zero();
    test_identity_copy();
    test_missing_input_exits_nonzero();
    test_nested_relative_input_mirrors_tree();

    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
