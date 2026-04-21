// pm4_two_plugins_independent.cpp — PM4-9 smoke 5 for the Registry's
// duplicate-`kind_id` collision detection across two runtime-loaded
// plugins.
//
// Scope (per issue `sturm-4oyr.10`)
// --------------------------------
// The PM4 implementation plan §Verification Smoke 5 pins:
//
//   > two plugins registering the same `kind_id` string cause a hard
//   > error at the second registration with a diagnosable message.
//
// The hard-error path lives in `plugin_registry.cpp:108-113`:
//
//     if (render_fns_.count(key) > 0) {
//         std::fprintf(stderr,
//                      "sturm-transpile plugin: duplicate op "
//                      "registration for kind_id '%s' (second "
//                      "registration rejected)\n",
//                      key.c_str());
//         std::abort();
//     }
//
// A regression that silently discards the second registration, drops
// the stderr line, or replaces `std::abort()` with a non-fatal path
// would be undetectable by the other PM4 smokes — this smoke is the
// last line of defence.
//
// How the collision is triggered end-to-end
// -----------------------------------------
// Two MODULE shared libraries are built from independent plugin TUs,
// each exporting its own `sturm_register_plugin_v1` that calls
// `register_op("pm4.demo.tag", ...)`:
//
//   1. `sturm-pm4-demo-plugin` (the first demo plugin at
//      `examples/plugin_demo/plugin_demo.cpp`) — the canonical demo.
//   2. `sturm-pm4-collision-plugin` (this smoke's fixture at
//      `transpiler/tests/fixtures/pm4_collision_plugin.cpp`) — the
//      independent second plugin whose sole purpose is to claim the
//      same kind_id.
//
// Both are loaded into a single clang++ invocation via two
// `-Xclang -plugin-arg-sturm-transpile -Xclang load=<path>` cc1 arg
// pairs. `ParseArgs` dlopens both in command-line order and queues
// them onto `runtime_registrars()`. The `TranspileConsumer` ctor
// drains that vector inside its per-consumer Registry: the first
// drain call registers `pm4.demo.tag` cleanly; the second drain call
// hits the duplicate check and calls `std::abort()` with the stderr
// line above.
//
// Observable outcomes
// -------------------
// The aborted process exits with a signal (SIGABRT, exit code 134 via
// the shell's `128 + signum` convention). `popen`/`pclose` surface
// this as a non-zero exit but the exact code varies across platforms
// — we therefore assert only "exit code != 0" and pivot on the
// stderr line for the contract-level gate.
//
// Why the in-tree dogfood migration does NOT cause this collision
// -----------------------------------------------------------------
// The PM4-6 dogfood migration registers `sturm.pb.{add,sub,mul,div}_
// assign_const` — four DIFFERENT kind_ids, none of which is
// `pm4.demo.tag`. The collision target string was picked by
// `plugin_demo.cpp:89` precisely because it has no in-tree analog, so
// only a SECOND plugin deliberately claiming `pm4.demo.tag` can
// trigger the collision.
//
// Relationship to the other PM4 smokes
// ------------------------------------
// Mirrors the failure-path posture of `pm4_smoke_dlopen` /
// `pm4_smoke_linktime` / `pm4_missing_plugin_errors`: exit 0 on
// success, non-zero on any contract violation, with a `.cmake`
// driver wrapper that relays stdout / stderr so `ctest -R
// pm4_two_plugins_independent` is self-contained.

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
#ifndef STURM_PM4_DEMO_PLUGIN_PATH
#error \
    "STURM_PM4_DEMO_PLUGIN_PATH must be defined to the first demo plugin .so"
#endif
#ifndef STURM_PM4_COLLISION_PLUGIN_PATH
#error \
    "STURM_PM4_COLLISION_PLUGIN_PATH must be defined to the collision .so"
#endif

const char* plugin_path()            { return STURM_PLUGIN_PATH; }
const char* clangxx_bin()            { return STURM_PLUGIN_CLANGXX; }
const char* demo_plugin_path()       { return STURM_PM4_DEMO_PLUGIN_PATH; }
const char* collision_plugin_path()  {
    return STURM_PM4_COLLISION_PLUGIN_PATH;
}

struct RunResult {
    int exit_code = -1;
    std::string combined_output;
};

// Run `cmd` through /bin/sh -c with merged stdout + stderr capture.
// Same shape as `pm4_smoke_dlopen.cpp:147-165` /
// `test_plugin_load.cpp:62-80`.
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
    // Preserve the shell-level exit code. On SIGABRT the shell reports
    // 128 + 6 = 134 (or similar via WIFSIGNALED); we only check !=0 so
    // either encoding is acceptable.
    r.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return r;
}

std::string tempdir() {
    char tmpl[] = "/tmp/sturm_pm4_two_plugins_XXXXXX";
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

bool file_exists_nonempty(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && st.st_size > 0;
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

// ── Gate A — both plugin `.so` artifacts exist on disk ────────────────
//
// Sanity check that the CMake build graph produced both MODULE
// libraries before the compile-time arg-pair chain is even worth
// invoking. A missing file here indicates a target-level
// `add_dependencies` regression in `transpiler/tests/CMakeLists.txt`,
// which would be a confusing failure mode if surfaced through a
// dlopen diagnostic from Gate B.
void gate_plugin_artifacts_exist() {
    CHECK_MSG(file_exists_nonempty(demo_plugin_path()),
              "first demo plugin MODULE missing — the build target "
              "`sturm-pm4-demo-plugin` did not produce a .so/.dylib. "
              "Rebuild `sturm-pm4-demo-plugin` and re-run.",
              (RunResult{-1, demo_plugin_path()}));
    CHECK_MSG(file_exists_nonempty(collision_plugin_path()),
              "collision plugin MODULE missing — the build target "
              "`sturm-pm4-collision-plugin` did not produce a .so/"
              ".dylib. Rebuild `sturm-pm4-collision-plugin` and "
              "re-run.",
              (RunResult{-1, collision_plugin_path()}));
}

// ── Gate B — two plugins claiming the same kind_id hard-abort ──────────
//
// The authoritative gate: load both plugins via two `load=<path>` cc1
// arg pairs. The first plugin's `register_op("pm4.demo.tag", ...)`
// succeeds against the per-consumer Registry; the second hits the
// duplicate-kind_id check in `plugin_registry.cpp:108-113` and calls
// `std::abort()`. The abort produces:
//
//   - Non-zero process exit (SIGABRT via `abort()`, shell encodes as
//     128+6=134 or WIFSIGNALED path).
//   - A stderr line containing `duplicate op registration for kind_id
//     'pm4.demo.tag'` — the exact spelling `plugin_registry.cpp:110`
//     emits.
//
// Note on plugin load order: under Clang 17 the `ParseArgs` loop at
// `plugin.cpp:328-362` scans the arg list in CLI order, collecting
// `load=<path>` tokens into `pending_loads`, then executes them in
// command-line order. We put the first demo plugin first and the
// collision plugin second; the collision happens during the consumer
// ctor's drain of `runtime_registrars()`, which iterates in insertion
// order, so the second plugin's `register_op` is the one that aborts.
// (If the order were reversed, the collision plugin would register
// cleanly first and the canonical plugin would abort second — the
// collision contract holds either way.)
void gate_two_plugins_same_kind_id_abort(const std::string& dir) {
    const std::string src = dir + "/collision.cpp";
    write_file(src, "int main() { return 0; }\n");

    const std::string obj = dir + "/collision.o";

    // Activate the plugin explicitly (same shape as
    // `test_plugin_load.cpp:168-170` / `pm4_missing_plugin_errors.cpp:
    // gate_nonexistent_plugin_errors`) — `-Xclang -load` +
    // `-Xclang -plugin sturm-transpile`. Without explicit activation,
    // `ParseArgs` never runs and the two `load=` tokens are
    // never processed.
    //
    // Two `load=` arg pairs thread both plugins through
    // `plugin.cpp`'s `load_runtime_plugin`. Both succeed at the
    // `dlopen → version check → dlsym` chain because both are valid
    // MODULE libraries built against the same Clang. The collision
    // only fires inside the consumer-ctor drain of the Registry's
    // `runtime_registrars()` vector.
    std::string cmd = std::string(clangxx_bin()) +
        " -Xclang -load -Xclang " + plugin_path() +
        " -Xclang -plugin -Xclang sturm-transpile" +
        " -Xclang -plugin-arg-sturm-transpile -Xclang load=" +
            demo_plugin_path() +
        " -Xclang -plugin-arg-sturm-transpile -Xclang load=" +
            collision_plugin_path() +
        " -c -o " + obj +
        " " + src;
    auto r = run(cmd);

    CHECK_MSG(r.exit_code != 0,
              "compile unexpectedly exited 0 despite two plugins "
              "both calling `register_op(\"pm4.demo.tag\", ...)`. The "
              "Registry's duplicate-kind_id check at "
              "plugin_registry.cpp:108-113 must `std::abort()` on the "
              "second registration, which surfaces as a non-zero "
              "process exit.",
              r);

    // Authoritative assertion: the documented collision diagnostic
    // line. A refactor of `Registry::register_op`'s error wording
    // fails this gate.
    CHECK_MSG(contains(r.combined_output,
                       "duplicate op registration for kind_id"),
              "stderr did not contain the PM4-2 duplicate-kind_id "
              "diagnostic `duplicate op registration for kind_id` — "
              "either the collision check at "
              "plugin_registry.cpp:108-113 regressed, or the second "
              "registration was silently discarded.",
              r);
    CHECK_MSG(contains(r.combined_output, "'pm4.demo.tag'"),
              "stderr did not spell the conflicting kind_id "
              "'pm4.demo.tag' in the diagnostic — the collision "
              "message MUST name the offending key so a user can "
              "trace it back to the registering plugins.",
              r);
    CHECK_MSG(contains(r.combined_output,
                       "second registration rejected"),
              "stderr did not spell `second registration rejected` — "
              "the documented trailing clause of the PM4-2 collision "
              "diagnostic regressed.",
              r);
}

// ── Gate C — loading only ONE plugin does NOT trip the collision ──────
//
// Control experiment: the SAME first plugin alone (no second plugin)
// must compile cleanly. This pins the "collision is specifically
// caused by TWO plugins claiming the SAME key" contract — if the
// collision line fired even when only one plugin was loaded, the
// Registry's collision detection would be broken in the opposite
// direction (false positives on a clean registration). This gate is
// the inverse image of Gate B.
//
// Note: under `STURM_PM4_LINK_DEMO=ON`, the first demo plugin's
// registrar is also baked into `sturm-transpile-plugin.so` at link
// time, so a `load=<demo>` against the same plugin creates the
// collision even with only one `load=` entry. We therefore guard
// Gate C on the build configuration: skip the single-plugin happy
// path when the link-time bake is enabled, same way
// `test_plugin_load.cpp:115-119` / `pm4_smoke_dlopen.cpp:322-326`
// weaken their equivalent gates.
void gate_single_plugin_registers_cleanly(const std::string& dir) {
#ifdef STURM_PM4_LINK_DEMO_ENABLED
    // Under STURM_PM4_LINK_DEMO=ON, the demo plugin's link-time
    // registrar is baked into sturm-transpile-plugin.so, so any
    // runtime `load=<demo>` collides with the baked-in copy before
    // this gate can fire. The consumer-ctor drain order is in-tree
    // → runtime → link-time (plan §6), so the second registration
    // is the link-time one. This is still a valid PM4-2 collision
    // test, but the diagnostic's spelling is already covered by
    // Gate B; skip this single-plugin happy-path gate under the
    // linked-demo config.
    std::fprintf(stderr,
                 "DEFER gate_single_plugin_registers_cleanly skipped "
                 "under STURM_PM4_LINK_DEMO=ON (the demo plugin is "
                 "baked into sturm-transpile-plugin.so at link time, "
                 "so a runtime load=<demo> already hits the "
                 "duplicate-kind_id collision). Gate B remains the "
                 "authoritative collision assertion for this build.\n");
    return;
#else
    const std::string src = dir + "/single.cpp";
    write_file(src, "int main() { return 0; }\n");

    const std::string obj = dir + "/single.o";

    // Only the first demo plugin — no second plugin, so the second
    // registration branch never runs and the compile must exit 0.
    std::string cmd = std::string(clangxx_bin()) +
        " -Xclang -load -Xclang " + plugin_path() +
        " -Xclang -plugin -Xclang sturm-transpile" +
        " -Xclang -plugin-arg-sturm-transpile -Xclang load=" +
            demo_plugin_path() +
        " -c -o " + obj +
        " " + src;
    auto r = run(cmd);

    CHECK_MSG(r.exit_code == 0,
              "single-plugin compile failed — the Registry must NOT "
              "flag a clean registration as a collision. A non-zero "
              "exit here indicates the PM4-2 duplicate-kind_id check "
              "has a false-positive path.",
              r);
    CHECK_MSG(!contains(r.combined_output,
                        "duplicate op registration"),
              "single-plugin compile surfaced the collision "
              "diagnostic — the Registry fired the `duplicate op "
              "registration` error on a clean registration. PM4-2's "
              "check must only trip when the kind_id was already "
              "present.",
              r);
#endif
}

}  // namespace

int main() {
    std::fprintf(stderr,
                 "pm4_two_plugins_independent: host plugin path:      %s\n"
                 "pm4_two_plugins_independent: clang++ bin:           %s\n"
                 "pm4_two_plugins_independent: demo plugin path:      %s\n"
                 "pm4_two_plugins_independent: collision plugin path: %s\n",
                 plugin_path(), clangxx_bin(),
                 demo_plugin_path(), collision_plugin_path());

    gate_plugin_artifacts_exist();

    std::string dir = tempdir();
    std::fprintf(stderr,
                 "pm4_two_plugins_independent: scratch dir:           %s\n",
                 dir.c_str());

    gate_two_plugins_same_kind_id_abort(dir);
    gate_single_plugin_registers_cleanly(dir);

    std::fprintf(stderr,
                 "\npm4_two_plugins_independent: %d/%d passed\n",
                 tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
