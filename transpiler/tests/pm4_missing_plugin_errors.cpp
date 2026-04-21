// pm4_missing_plugin_errors.cpp — PM4-9 smoke 4 for the missing-plugin
// error contract.
//
// Scope (per issue `sturm-4oyr.10`)
// --------------------------------
// The PM4 implementation plan §Verification Smoke 4 pins:
//
//   > `PLUGINS /nonexistent.so` fails the compile with a recognizable
//   > `sturm-transpile plugin: failed to dlopen` stderr line.
//
// The exact stderr prefix is emitted by `plugin.cpp:195-198`:
//
//     llvm::errs() << "sturm-transpile plugin: failed to dlopen "
//                  << path << ": "
//                  << (err != nullptr ? err : "(unknown error)")
//                  << "\n";
//
// A regression that silently changes the diagnostic wording, drops the
// stderr line, or returns `true` from `ParseArgs` on a `dlopen` failure
// would pass the existing gates (`test_plugin_load`'s
// `gate_load_nonexistent_errors`) but miss the end-to-end "the CMake
// PLUGINS pipeline also errors out" wiring. PM4-9's smoke 4 pins the
// full chain: CMake `PLUGINS` → `-Xclang -plugin-arg-sturm-transpile
// -Xclang load=<bogus path>` cc1 token → `ParseArgs` → `dlopen` → the
// exact stderr line above → non-zero compile exit.
//
// What this smoke exercises vs. what `test_plugin_load` already pins
// ------------------------------------------------------------------
// `test_plugin_load.cpp:418-441` (`gate_load_nonexistent_errors`)
// already drives the cc1 arg pair directly and asserts on the stderr
// line. This PM4-9 smoke is the PM4-focused parallel: it is declared
// inside `transpiler/tests/` alongside the other `pm4_smoke_*`
// binaries so `ctest -R pm4_` sees it in the full PM4 smoke set, and
// it also exercises the code path by invoking the clang++ driver with
// the `-Xclang -plugin-arg-sturm-transpile -Xclang load=<path>` token
// pattern (the same pattern the `add_quantum_executable(... PLUGINS
// <path>)` helper emits at `cmake/SturmTranspile.cmake:315-348`).
//
// Why not also drive `add_quantum_executable(... PLUGINS /nonexistent)`
// ---------------------------------------------------------------------
// `add_quantum_executable` is a configure-time helper — a failing
// compile from a PLUGINS path pointing at a nonexistent `.so` would
// break `cmake --build build` for the whole tree, not just this test.
// Ctest operates at build-time; a test that needs the failing target
// to BUILD successfully cannot also claim the build itself is
// non-zero. We therefore drive the same cc1 arg pattern through
// `clang++` directly, the way `test_plugin_load` does, and assert the
// stderr contract + non-zero compile exit. The PM4-5 integration test
// at `tests/transpiler/test_cmake_plugins/` covers the happy-path
// CMake glue end-to-end; this smoke covers the error path without
// depending on a configure-time build failure.
//
// Relationship to `pm4_smoke_dlopen` / `pm4_smoke_linktime` drivers
// -----------------------------------------------------------------
// Mirrors their failure-path posture: exit 0 on success, non-zero on
// any contract violation, with a `.cmake` driver wrapper that relays
// stdout / stderr so `ctest -R pm4_missing_plugin_errors` is self-
// contained.

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

#ifndef STURM_PLUGIN_PATH
#error \
    "STURM_PLUGIN_PATH must be defined to the sturm-transpile-plugin .so path"
#endif
#ifndef STURM_PLUGIN_CLANGXX
#error "STURM_PLUGIN_CLANGXX must be defined to the clang++ driver path"
#endif

const char* plugin_path() { return STURM_PLUGIN_PATH; }
const char* clangxx_bin() { return STURM_PLUGIN_CLANGXX; }

struct RunResult {
    int exit_code = -1;
    std::string combined_output;
};

// Run `cmd` through /bin/sh -c with merged stdout + stderr capture.
// Same shape as `pm4_smoke_dlopen.cpp:147-165` / `test_plugin_load.cpp:
// 62-80`.
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

std::string tempdir() {
    char tmpl[] = "/tmp/sturm_pm4_missing_plugin_XXXXXX";
    if (!::mkdtemp(tmpl)) {
        std::fprintf(stderr, "mkdtemp failed: %s\n", std::strerror(errno));
        std::exit(2);
    }
    return std::string(tmpl);
}

void write_file(const std::string& path, const std::string& body) {
    std::ofstream os(path, std::ios::binary);
    os << body;
}

bool contains(const std::string& h, const std::string& n) {
    return h.find(n) != std::string::npos;
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

#define CHECK_MSG(cond, msg, r) do {                                  \
    ++tests_run;                                                      \
    if (cond) { ++tests_pass; }                                       \
    else {                                                            \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                     \
                     __FILE__, __LINE__, #cond);                      \
        std::fprintf(stderr, "    %s\n", (msg));                      \
        std::fprintf(stderr, "    exit=%d output:\n%s\n",             \
                     (r).exit_code,                                   \
                     (r).combined_output.c_str());                    \
    }                                                                 \
} while (0)

// ── Gate A — `load=<nonexistent>` produces the documented error line ──
//
// Exercise the exact cc1 arg pattern the `add_quantum_executable(...
// PLUGINS <path>)` helper emits for every PLUGINS entry
// (`cmake/SturmTranspile.cmake:315-348`):
//
//     -Xclang -plugin-arg-sturm-transpile -Xclang load=<abs-plugin-path>
//
// point it at a path that is guaranteed NOT to exist on any CI host
// (`/proc/self/nonexistent-pm4-smoke.so` — the `/proc/self/` prefix
// defeats tempdir-cleanup races with an unrelated test leaving a
// like-named artifact behind), and assert:
//
//   - The compile exits non-zero (ParseArgs returns false from
//     `load_runtime_plugin`, Clang aborts plugin setup → non-zero cc1
//     exit).
//   - stderr contains the `sturm-transpile plugin: failed to dlopen`
//     prefix byte-for-byte — the exact string `plugin.cpp:195` emits.
//     A drift in the wording (capitalisation, punctuation, plural
//     form) fails this gate.
//   - stderr contains the offending path verbatim so a user can see
//     which PLUGINS entry was the culprit.
//   - stderr does NOT spell `loaded runtime plugin` — the success
//     branch at `plugin.cpp:258` must not run.
//   - stderr does NOT spell `pm4.demo.tag` — no plugin registered
//     anything, so the probe-Registry kind_ids trace must not
//     fabricate a kind_id.
void gate_nonexistent_plugin_errors(const std::string& dir) {
    const std::string src = dir + "/load_missing.cpp";
    write_file(src, "int main() { return 0; }\n");

    const std::string obj = dir + "/load_missing.o";

    // The nonexistent path. `/proc/self/` is a synthetic filesystem
    // that cannot host an arbitrary `.so` name, so no race with a
    // stray artifact can mask the failure.
    const std::string bogus =
        "/proc/self/nonexistent-pm4-smoke-plugin.so";

    // Activate the plugin explicitly (same shape as
    // `test_plugin_load.cpp:168-170`) — `-Xclang -load` + `-Xclang
    // -plugin sturm-transpile`. Without explicit activation, a
    // ReplaceAction plugin's `ParseArgs` never runs and the
    // `load=<bogus>` token is never parsed, so the `dlopen`
    // failure branch never fires.
    std::string cmd = std::string(clangxx_bin()) +
        " -Xclang -load -Xclang " + plugin_path() +
        " -Xclang -plugin -Xclang sturm-transpile" +
        " -Xclang -plugin-arg-sturm-transpile -Xclang load=" +
            bogus +
        " -c -o " + obj +
        " " + src;
    auto r = run(cmd);

    CHECK_MSG(r.exit_code != 0,
              "compile unexpectedly exited 0 with a load=<nonexistent> "
              "cc1 arg — `ParseArgs` must return false on a failed "
              "dlopen, which Clang surfaces as a non-zero cc1 exit.",
              r);

    // Authoritative assertion — the exact stderr prefix the issue
    // description calls out. A refactor of `plugin.cpp`'s
    // `load_runtime_plugin` error path that drifts from this wording
    // is a test failure.
    CHECK_MSG(contains(r.combined_output,
                       "sturm-transpile plugin: failed to dlopen"),
              "stderr did not contain the PM4-4 dlopen-failure "
              "diagnostic prefix `sturm-transpile plugin: failed to "
              "dlopen` — either `plugin.cpp:195-198` regressed its "
              "error wording or the failure path did not run.",
              r);

    // The offending path must appear verbatim so a user debugging a
    // CI failure can immediately identify the culprit PLUGINS entry.
    CHECK_MSG(contains(r.combined_output, bogus),
              "stderr did not spell the nonexistent plugin path "
              "verbatim — the `failed to dlopen` line must include the "
              "path so users can trace the failure to the specific "
              "PLUGINS entry.",
              r);

    // Negative checks: the success branches must NOT fire.
    CHECK_MSG(!contains(r.combined_output, "loaded runtime plugin"),
              "stderr contains `loaded runtime plugin` — the success "
              "branch at plugin.cpp:258 must never run when the "
              "dlopen failed.",
              r);
    CHECK_MSG(!contains(r.combined_output, "pm4.demo.tag"),
              "stderr contains `pm4.demo.tag` — the probe-Registry "
              "`registered kind_ids:` trace must not fabricate a "
              "kind_id when no plugin registered anything.",
              r);
}

// ── Gate B — verbose=on still emits the `failed to dlopen` line ──────
//
// The `verbose` cc1 arg MUST NOT convert a fatal dlopen failure into a
// silent/partial pass. When the `verbose` toggle is paired with a
// `load=<bogus>` token, `load_runtime_plugin` still returns false
// after the error line, and `ParseArgs` must still surface a non-zero
// exit. Pinning this separately catches a hypothetical refactor where
// someone tried to downgrade the error to a verbose-only diagnostic.
void gate_nonexistent_plugin_errors_with_verbose(const std::string& dir) {
    const std::string src = dir + "/load_missing_verbose.cpp";
    write_file(src, "int main() { return 0; }\n");

    const std::string obj = dir + "/load_missing_verbose.o";
    const std::string bogus =
        "/proc/self/nonexistent-pm4-smoke-plugin-verbose.so";

    std::string cmd = std::string(clangxx_bin()) +
        " -Xclang -load -Xclang " + plugin_path() +
        " -Xclang -plugin -Xclang sturm-transpile" +
        " -Xclang -plugin-arg-sturm-transpile -Xclang verbose" +
        " -Xclang -plugin-arg-sturm-transpile -Xclang load=" +
            bogus +
        " -c -o " + obj +
        " " + src;
    auto r = run(cmd);

    CHECK_MSG(r.exit_code != 0,
              "verbose + load=<nonexistent> compile unexpectedly "
              "exited 0 — a verbose-mode toggle must not suppress the "
              "dlopen-failure path's non-zero exit.",
              r);
    CHECK_MSG(contains(r.combined_output,
                       "sturm-transpile plugin: failed to dlopen"),
              "verbose + load=<nonexistent> stderr did not contain the "
              "PM4-4 dlopen-failure diagnostic prefix — the verbose "
              "flag must NOT suppress the error line.",
              r);
}

}  // namespace

int main() {
    std::fprintf(stderr,
                 "pm4_missing_plugin_errors: host plugin path:  %s\n"
                 "pm4_missing_plugin_errors: clang++ bin:       %s\n",
                 plugin_path(), clangxx_bin());

    std::string dir = tempdir();
    std::fprintf(stderr,
                 "pm4_missing_plugin_errors: scratch dir:      %s\n",
                 dir.c_str());

    gate_nonexistent_plugin_errors(dir);
    gate_nonexistent_plugin_errors_with_verbose(dir);

    std::fprintf(stderr,
                 "\npm4_missing_plugin_errors: %d/%d passed\n",
                 tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
