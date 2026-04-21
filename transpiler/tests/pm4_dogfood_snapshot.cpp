// pm4_dogfood_snapshot.cpp — PM4-9 smoke 3 for the post-PM4-6 dogfood
// migration byte-identity gate.
//
// Scope (per issue `sturm-4oyr.10`)
// --------------------------------
// The PM4 implementation plan §Verification Smoke 3 pins the claim that
// the PM4-6 dogfood migration of the four Phase B qint_t compound-assign
// matchers (`ADD_ASSIGN_CONST` / `SUB_ASSIGN_CONST` / `MUL_ASSIGN_CONST`
// / `DIV_ASSIGN_CONST`) is **transparent to the rewritten-output
// surface**: the byte sequence the standalone driver emits for the
// canonical `a += k;` / `a -= k;` / `a *= k;` / `a /= k;` fixtures is
// unchanged from pre-migration.
//
// What this smoke pins vs. what the other PB gates pin
// -----------------------------------------------------
// The in-tree snapshot CTests (`snapshot_{add,sub,mul,div}_assign_const`
// in `tests/transpiler/CMakeLists.txt:125-171`) already diff the
// transpiled fixture against `*.expected.cpp` byte-for-byte. This
// PM4-9 smoke is the **PM4-focused parallel** to that gate: it is
// declared inside `transpiler/tests/` alongside the other `pm4_smoke_*`
// binaries so a reviewer running `ctest -R pm4_` sees every PM4 smoke
// in one set, and so a future PM4 refactor that regresses the dogfood
// migration (dropping a `register_matcher` call, mutating a
// QIntAssignConstCallback template argument, etc.) surfaces here
// BEFORE the generic snapshot gates fire — with a PM4-4 /
// PM4-6-scoped failure diagnostic rather than a generic "byte delta".
//
// The assertion logic is simple:
//   1. Build the standalone `sturm-transpile` binary (build-order dep
//      wired by the CMake target definition).
//   2. For each of the four PB fixtures under
//      `tests/transpiler/fixtures/{add,sub,mul,div}_assign_const.cpp`,
//      run the binary with `--output-dir=<scratch>` and compare the
//      generated sibling against the fixture's locked-down
//      `.expected.cpp` golden byte-for-byte.
//   3. Any byte delta fails the test with a diagnostic naming the
//      offending fixture + dumping a unified diff when available.
//
// Relationship to the `pm4_smoke_dlopen` / `pm4_smoke_linktime`
// drivers
// -------------------------------------------------------------
// This smoke mirrors the failure-path posture of those drivers: the
// binary exits 0 on success, non-zero on any contract violation, and
// the companion `pm4_dogfood_snapshot.cmake` script relays stdout /
// stderr into the ctest log so `ctest -R pm4_dogfood_snapshot` is
// self-contained.

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

#ifndef STURM_TRANSPILE_BIN
#error "STURM_TRANSPILE_BIN must be defined to the sturm-transpile binary"
#endif
#ifndef STURM_PM4_FIXTURES_DIR
#error \
    "STURM_PM4_FIXTURES_DIR must be defined to tests/transpiler/fixtures/"
#endif
#ifndef STURM_PM4_SCRATCH_DIR
#error "STURM_PM4_SCRATCH_DIR must be defined to a writable scratch directory"
#endif

const char* transpile_bin() { return STURM_TRANSPILE_BIN; }
const char* fixtures_dir() { return STURM_PM4_FIXTURES_DIR; }
const char* scratch_dir()  { return STURM_PM4_SCRATCH_DIR; }

struct RunResult {
    int exit_code = -1;
    std::string combined_output;
};

// Run `cmd` through /bin/sh -c with merged stdout + stderr capture. Same
// shape as `pm4_smoke_dlopen.cpp:147-165` so a reviewer has one mental
// model for every PM4 smoke's shell-out helper.
RunResult run(const std::string& cmd) {
    std::string full = cmd + " 2>&1";
    FILE* fp = ::popen(full.c_str(), "r");
    if (!fp) {
        RunResult r;
        r.combined_output = "popen failed";
        return r;
    }
    std::ostringstream oss;
    char buf[4096];
    while (std::fgets(buf, sizeof buf, fp)) {
        oss << buf;
    }
    int status = ::pclose(fp);
    RunResult r;
    r.combined_output = oss.str();
    r.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return r;
}

std::string read_file_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

bool file_exists_nonempty(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && st.st_size > 0;
}

// Recursive `mkdir -p` equivalent. The scratch-dir root threaded in
// via STURM_PM4_SCRATCH_DIR may sit several levels deep under the
// build tree (e.g. `build/transpiler/tests/gen/pm4_dogfood_snapshot`)
// and CMake does not pre-create that directory — the ctest driver
// only knows about the ctest binary's working directory. Walk the
// path left-to-right, creating each segment that does not exist yet.
void ensure_dir(const std::string& path) {
    if (path.empty()) return;
    // Fast path: already exists.
    struct stat st{};
    if (::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return;

    // Walk parents left-to-right.
    std::string accum;
    for (size_t i = 0; i < path.size(); ++i) {
        accum.push_back(path[i]);
        if (path[i] == '/' || i + 1 == path.size()) {
            if (accum == "/") continue;
            if (::mkdir(accum.c_str(), 0755) == 0) continue;
            if (errno == EEXIST) continue;
            std::fprintf(stderr,
                         "mkdir(%s) failed: %s\n", accum.c_str(),
                         std::strerror(errno));
            std::exit(2);
        }
    }
}

// ── Test harness ──────────────────────────────────────────────────────────
int tests_run  = 0;
int tests_pass = 0;

#define CHECK(cond) do {                                              \
    ++tests_run;                                                      \
    if (cond) { ++tests_pass; }                                       \
    else {                                                            \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                     \
                     __FILE__, __LINE__, #cond);                      \
    }                                                                 \
} while (0)

#define CHECK_MSG(cond, msg) do {                                     \
    ++tests_run;                                                      \
    if (cond) { ++tests_pass; }                                       \
    else {                                                            \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                     \
                     __FILE__, __LINE__, #cond);                      \
        std::fprintf(stderr, "    %s\n", (msg));                      \
    }                                                                 \
} while (0)

// Transpile `<fixture_base>.cpp` and byte-compare the generated sibling
// against `<fixture_base>.expected.cpp`. On mismatch, emit a unified
// diff when `diff` is reachable so a ctest log reader can pinpoint the
// delta without re-running the snapshot under a debugger.
//
// `fixture_base` is the stem without extension — e.g. "add_assign_const"
// for the PB-1 fixture. We invoke the transpiler from the fixture
// directory with a relative input path so the emitted `Source:` header
// line is deterministic (same discipline `run_snapshot.cmake` uses).
void gate_fixture_byte_identical(const std::string& fixture_base) {
    const std::string fixture_src =
        std::string(fixtures_dir()) + "/" + fixture_base + ".cpp";
    const std::string fixture_golden =
        std::string(fixtures_dir()) + "/" + fixture_base + ".expected.cpp";

    CHECK_MSG(file_exists_nonempty(fixture_src),
              ("PB fixture source missing or empty: " + fixture_src)
                  .c_str());
    CHECK_MSG(file_exists_nonempty(fixture_golden),
              ("PB fixture golden missing or empty: " + fixture_golden)
                  .c_str());

    // Fresh per-fixture scratch dir so a stale run of a previous
    // fixture cannot mask a regression on the current one.
    const std::string out_dir =
        std::string(scratch_dir()) + "/" + fixture_base;
    // Best-effort rm -rf via shell (no std::filesystem to keep the
    // binary dependency-free; posix `unlink` of the single expected
    // output below is sufficient but `rm -rf` is safer if a prior
    // partial run left a hierarchy behind).
    (void)run("rm -rf '" + out_dir + "'");
    ensure_dir(out_dir);

    // Invoke the standalone driver from the fixture's parent dir with
    // a relative input path. The emitter writes the passed path
    // verbatim into the `// Source:` header line; a relative path
    // keeps the emitted header byte-identical across build trees
    // (the golden file was captured with a relative path).
    std::string cmd = std::string(transpile_bin()) +
                      " " + fixture_base + ".cpp" +
                      " --output-dir " + out_dir;
    std::string full = "cd '" + std::string(fixtures_dir()) + "' && " + cmd;
    auto r = run(full);

    CHECK_MSG(r.exit_code == 0,
              ("sturm-transpile exited non-zero on " + fixture_base +
               ".cpp; output:\n" + r.combined_output).c_str());

    const std::string generated = out_dir + "/" + fixture_base + ".cpp";
    CHECK_MSG(file_exists_nonempty(generated),
              ("sturm-transpile exited 0 but did not produce "
               + generated).c_str());

    const std::string got      = read_file_all(generated);
    const std::string expected = read_file_all(fixture_golden);

    // The authoritative byte-identity gate. If this assertion fails
    // the PM4-6 dogfood migration regressed one of the four PB
    // matchers — the QOpKind emitted, the operand text captured, the
    // renderer output, or the consumer-ctor drain order.
    ++tests_run;
    if (got == expected) {
        ++tests_pass;
        std::fprintf(stderr,
                     "OK    %s.cpp: generated sibling is byte-identical "
                     "to %s.expected.cpp (%zu bytes).\n",
                     fixture_base.c_str(),
                     fixture_base.c_str(),
                     got.size());
    } else {
        std::fprintf(stderr,
                     "FAIL  %s.cpp: post-PM4-6 generated sibling DIFFERS "
                     "from %s.expected.cpp byte-for-byte.\n"
                     "  generated: %s (%zu bytes)\n"
                     "  expected:  %s (%zu bytes)\n",
                     fixture_base.c_str(),
                     fixture_base.c_str(),
                     generated.c_str(), got.size(),
                     fixture_golden.c_str(), expected.size());

        // Best-effort unified diff. `diff` is not always present on a
        // stripped CI image; a missing diff tool must NOT mask the
        // byte-compare failure, so we only emit the diff as a
        // diagnostic aid above the authoritative CHECK failure.
        auto dr = run("diff -u '" + fixture_golden + "' '" + generated + "'");
        if (!dr.combined_output.empty()) {
            std::fprintf(stderr,
                         "unified diff (expected vs generated):\n%s\n",
                         dr.combined_output.c_str());
        }
    }
}

}  // namespace

int main() {
    std::fprintf(stderr,
                 "pm4_dogfood_snapshot: transpile bin:   %s\n"
                 "pm4_dogfood_snapshot: fixtures dir:    %s\n"
                 "pm4_dogfood_snapshot: scratch dir:     %s\n",
                 transpile_bin(), fixtures_dir(), scratch_dir());

    // Make sure the scratch-dir root exists (the ctest driver passes
    // a path under the build tree; CMake creates it, but we defend
    // against a stale build tree where the dir was removed).
    ensure_dir(std::string(scratch_dir()));

    // Four PB fixtures — one per compound-assign operator. The fixture
    // names match the `snapshot_*_assign_const` ctest names in
    // `tests/transpiler/CMakeLists.txt`.
    gate_fixture_byte_identical("add_assign_const");
    gate_fixture_byte_identical("sub_assign_const");
    gate_fixture_byte_identical("mul_assign_const");
    gate_fixture_byte_identical("div_assign_const");

    std::fprintf(stderr,
                 "\npm4_dogfood_snapshot: %d/%d passed\n",
                 tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
