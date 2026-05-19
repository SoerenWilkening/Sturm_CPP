// test_io.cpp — unit tests for transpile/io.hpp helpers (M4)
//
// Covers:
//   - resolve_output_path(input, output_dir) produces
//       <output_dir>/<basename-of-input-path> for a relative input, and
//       mirrors the absolute/relative characteristic of the input under the
//       output dir when given a relative input path.
//   - write_file(path, bytes) creates parent directories if missing,
//       writes exact byte content, and returns the number of bytes written.
//   - read_file(path) round-trips the written bytes.
//   - Missing input reports an empty/failed result.
//
// The helpers live in a separate header/source so the main driver can call
// them without pulling in LibTooling, and so they are unit-testable without
// spawning a process.

#include "sturm/transpile/io.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace sturm_test_io_ns {

namespace fs = std::filesystem;
using namespace sturm::transpile;

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

// Returns a unique, empty scratch directory under the system temp dir.
static fs::path make_scratch_dir(const char* tag) {
    fs::path base = fs::temp_directory_path() / "sturm-m4-io-tests";
    fs::create_directories(base);
    fs::path dir = base / tag;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

// ── resolve_output_path ───────────────────────────────────────────────────────

static void test_resolve_relative_input() {
    fs::path out = resolve_output_path("src/demo.cpp", "/tmp/gen");
    // Expected: /tmp/gen/src/demo.cpp — relative components are preserved so
    // the generated tree mirrors the source tree.
    CHECK(out == fs::path("/tmp/gen/src/demo.cpp"));
}

static void test_resolve_basename_only() {
    fs::path out = resolve_output_path("hello.cpp", "/tmp/gen");
    CHECK(out == fs::path("/tmp/gen/hello.cpp"));
}

static void test_resolve_absolute_input_uses_basename() {
    // Absolute input: only the filename is placed under output_dir. The
    // transpiler does not mirror the host filesystem layout.
    fs::path out = resolve_output_path("/usr/local/src/foo.cpp", "/tmp/gen");
    CHECK(out == fs::path("/tmp/gen/foo.cpp"));
}

static void test_resolve_nested_relative_input() {
    fs::path out = resolve_output_path("a/b/c/foo.cpp", "/tmp/gen");
    CHECK(out == fs::path("/tmp/gen/a/b/c/foo.cpp"));
}

// ── write_file / read_file round-trip ─────────────────────────────────────────

static void test_write_file_creates_parent_dirs() {
    fs::path dir = make_scratch_dir("write_parents");
    fs::path target = dir / "a" / "b" / "c" / "out.txt";
    const std::string payload = "hello, transpile\n";
    bool ok = write_file(target, payload);
    CHECK(ok);
    CHECK(fs::exists(target));
    CHECK(fs::file_size(target) == payload.size());
}

static void test_write_file_round_trip() {
    fs::path dir = make_scratch_dir("round_trip");
    fs::path target = dir / "demo.cpp";
    const std::string payload =
        "#include <sturm/sturm.hpp>\n"
        "// binary-safe: \x01\x02\x03\n";
    CHECK(write_file(target, payload));
    std::string got;
    CHECK(read_file(target, got));
    CHECK(got == payload);
}

static void test_read_file_missing_returns_false() {
    fs::path dir = make_scratch_dir("missing");
    fs::path target = dir / "does_not_exist.cpp";
    std::string got = "not empty";
    bool ok = read_file(target, got);
    CHECK(!ok);
    // On failure the out-param is cleared so the caller cannot accidentally
    // use stale data.
    CHECK(got.empty());
}

}  // namespace sturm_test_io_ns

int main() {
    using namespace sturm_test_io_ns;
    using sturm_test_io_ns::tests_run;
    using sturm_test_io_ns::tests_pass;
    test_resolve_relative_input();
    test_resolve_basename_only();
    test_resolve_absolute_input_uses_basename();
    test_resolve_nested_relative_input();
    test_write_file_creates_parent_dirs();
    test_write_file_round_trip();
    test_read_file_missing_returns_false();

    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
