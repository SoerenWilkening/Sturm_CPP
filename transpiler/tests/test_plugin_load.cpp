// test_plugin_load.cpp — PM1-3 integration test for the sturm-transpile
// Clang plugin.
//
// Exercises the gate the PM1-3 issue calls out:
//
//   clang++ -fplugin=$<TARGET_FILE:sturm-transpile-plugin> -c hello.cpp
//             -o hello.o
//
// The plugin is expected to LOAD cleanly and the parent parse to run
// without diagnostics, even though the plugin's `getActionType()` is
// ReplaceAction (i.e. object-file emission is PM1-4's territory, not
// ours here). We therefore invoke the plugin via `-fsyntax-only` for
// the "plugin loads + parses trivial TU" gate and separately check
// that ParseArgs accepts the documented -verbose and -dump-to flags.
//
// Kept as a standalone `int main()` harness (not googletest) so the
// test has zero fixture-infrastructure dependencies — ctest treats
// exit 0 as pass, anything else as fail.
//
// The harness writes scratch files under a temp dir, shells out to
// the clang++ binary that was located at configure time (same one the
// nimr spike used), and asserts exit codes / stderr substrings /
// artifact existence.

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
#error "STURM_PLUGIN_PATH must be defined to the plugin shared-library path"
#endif
#ifndef STURM_PLUGIN_CLANGXX
#error "STURM_PLUGIN_CLANGXX must be defined to the clang++ driver path"
#endif
#ifndef STURM_PM4_DEMO_PLUGIN_PATH
#error \
    "STURM_PM4_DEMO_PLUGIN_PATH must be defined to the demo plugin .so path"
#endif

const char* plugin_path() { return STURM_PLUGIN_PATH; }
const char* clangxx_bin() { return STURM_PLUGIN_CLANGXX; }
const char* demo_plugin_path() { return STURM_PM4_DEMO_PLUGIN_PATH; }

struct RunResult {
    int exit_code = -1;
    std::string combined_output;
};

// Run `cmd` through /bin/sh -c and capture the merged stdout+stderr
// stream. We want stderr because that is where the plugin's -verbose
// and error messages go; we capture stdout too so any accidental
// stdout-noise from the plugin surfaces in failure logs.
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
    char tmpl[] = "/tmp/sturm_plugin_load_XXXXXX";
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

bool file_exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && st.st_size > 0;
}

bool contains(const std::string& h, const std::string& n) {
    return h.find(n) != std::string::npos;
}

int tests_run = 0;
int tests_pass = 0;

#define CHECK(cond) do {                                                \
    ++tests_run;                                                        \
    if (cond) { ++tests_pass; }                                         \
    else {                                                              \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                       \
                     __FILE__, __LINE__, #cond);                        \
    }                                                                   \
} while (0)

#define CHECK_MSG(cond, msg, r) do {                                    \
    ++tests_run;                                                        \
    if (cond) { ++tests_pass; }                                         \
    else {                                                              \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                       \
                     __FILE__, __LINE__, #cond);                        \
        std::fprintf(stderr, "    %s\n", (msg));                        \
        std::fprintf(stderr, "    exit=%d output:\n%s\n",               \
                     (r).exit_code,                                     \
                     (r).combined_output.c_str());                      \
    }                                                                   \
} while (0)

// `-fplugin=<path>` tells clang to dlopen the plugin, but Clang only
// *activates* a ReplaceAction plugin when the user opts in with
// `-Xclang -plugin -Xclang <name>`. Both invocation shapes matter:
//
//   * `-fplugin=<path>` alone — the PM1-3 gate command. The plugin
//     loads, registers into the FrontendPluginRegistry, and then does
//     nothing (because ReplaceAction plugins require explicit
//     activation). The compile proceeds exactly as if the plugin
//     weren't there, which is what the issue's acceptance criterion
//     spells out.
//   * `-Xclang -plugin -Xclang sturm-transpile` — activates the plugin
//     as the frontend action. This is the path we exercise to verify
//     the consumer wiring / ParseArgs flag handling actually runs.
std::string plugin_load_only_args() {
    return std::string(" -fplugin=") + plugin_path();
}

std::string plugin_active_args() {
    return std::string(" -Xclang -load -Xclang ") + plugin_path() +
           " -Xclang -plugin -Xclang sturm-transpile";
}

// Emit a `-plugin-arg-sturm-transpile <value>` pair via -Xclang so the
// cc1 invocation hands `<value>` to our plugin's ParseArgs. The user-
// facing `-fplugin-arg-<plugin>-<arg>` driver flag unfortunately
// splits on the FIRST hyphen after `-fplugin-arg-`, so a hyphenated
// plugin name like "sturm-transpile" sees the driver mis-parse the
// token into `-plugin-arg-sturm transpile-<rest>` (plugin name
// "sturm", arg "transpile-<rest>"). Going through -Xclang sidesteps
// the driver and lets us spell out the cc1 arg pair verbatim.
std::string plugin_arg_pair(const std::string& arg_value) {
    return std::string(" -Xclang -plugin-arg-sturm-transpile -Xclang ") +
           arg_value;
}

// Gate 1 — the plugin loads on a non-Sturm translation unit without
// diagnostics.
//
// The PM1-3 issue's gate command verbatim:
//
//   clang++ -fplugin=<.so> -c hello.cpp -o hello.o
//
// PM1-3 is ONLY the plugin skeleton — the nested CompilerInvocation
// wiring that would substitute our rewritten buffer back into
// EmitObjAction belongs to PM1-4 (sturm-bkcr). Since we register
// `ReplaceAction` as the action-type contract for PM1-4, and Clang
// auto-activates `-fplugin=<path>` plugins, the default EmitObj
// action is replaced by the plugin's consumer. The consumer runs,
// sees zero Sturm ops, and returns cleanly — so the process exits
// with code 0, which is what the gate's "succeeds" check actually
// probes. The hello.o file is NOT produced at PM1-3 (it will be at
// PM1-4 once ExecuteAction is overridden to run a nested
// EmitObjAction over the rewritten buffer). At PM1-3 we therefore
// assert exit-0 + no error diagnostics + silent consumer —
// object-file existence is PM1-4's gate to clear.
void gate_plugin_loads_on_non_sturm(const std::string& dir) {
    const std::string src = dir + "/hello.cpp";
    write_file(src,
        "#include <cstdio>\n"
        "int main() { std::printf(\"%d\\n\", 42); return 0; }\n");

    const std::string obj = dir + "/hello.o";
    std::string cmd = std::string(clangxx_bin()) +
        plugin_load_only_args() +
        " -c -o " + obj +
        " " + src;
    auto r = run(cmd);
    CHECK_MSG(r.exit_code == 0,
              "plugin load failed on non-Sturm TU", r);
    // Clang writes "error: " for hard errors and "warning: " for
    // non-fatal diagnostics. Neither should appear on a clean TU.
    CHECK_MSG(!contains(r.combined_output, "error:"),
              "plugin load produced an error diagnostic", r);
    CHECK_MSG(!contains(r.combined_output, "fatal error"),
              "plugin load produced a fatal diagnostic", r);
    // No plugin output should escape in the default (non-verbose)
    // configuration — the plugin must behave transparently.
    CHECK_MSG(!contains(r.combined_output, "sturm-transpile plugin:"),
              "plugin emitted verbose output without -verbose flag",
              r);
}

// Gate 2 — `-fplugin-arg-sturm-transpile-verbose` makes the plugin emit
// its one-liner per TU. We key off the "rewritten_buffer() size=" token
// the plugin prints in EndSourceFileAction under -verbose.
void gate_verbose_flag_prints_trace(const std::string& dir) {
    const std::string src = dir + "/verbose.cpp";
    write_file(src, "int main() { return 0; }\n");

    // Activate the plugin explicitly so ReplaceAction → ParseArgs →
    // CreateASTConsumer → EndSourceFileAction actually run. The
    // -verbose arg is forwarded via -fplugin-arg-<name>-<key>. We
    // pass -c so the driver does not try to LINK a .o the
    // ReplaceAction never emitted (which would fail with an
    // undefined-`main` link error in clang++).
    const std::string obj = dir + "/verbose.o";
    std::string cmd = std::string(clangxx_bin()) +
        plugin_active_args() +
        plugin_arg_pair("verbose") +
        " -c -o " + obj +
        " " + src;
    auto r = run(cmd);
    CHECK_MSG(r.exit_code == 0, "plugin activation (verbose) failed", r);
    CHECK_MSG(contains(r.combined_output, "sturm-transpile plugin:"),
              "-verbose did not emit the plugin trace line", r);
    CHECK_MSG(contains(r.combined_output, "rewritten_buffer() size="),
              "-verbose output missing rewritten_buffer size token", r);
}

// Gate 3 — `-fplugin-arg-sturm-transpile-dump-to=<path>` writes the
// rewritten buffer to <path>.  On a non-Sturm TU the buffer is
// effectively the input (the Rewriter produces no mutations), so we
// assert the dump file exists and contains the `int main` token we
// wrote into the input.
void gate_dump_to_flag_writes_file(const std::string& dir) {
    const std::string src = dir + "/dump.cpp";
    const std::string dump = dir + "/dump.out.cpp";
    write_file(src,
        "// PM1-3 non-Sturm translation unit\n"
        "int main() { return 0; }\n");

    const std::string obj = dir + "/dump.o";
    std::string cmd = std::string(clangxx_bin()) +
        plugin_active_args() +
        plugin_arg_pair("dump-to=" + dump) +
        " -c -o " + obj +
        " " + src;
    auto r = run(cmd);
    CHECK_MSG(r.exit_code == 0,
              "plugin load (dump-to) failed", r);
    CHECK_MSG(file_exists(dump),
              "plugin did not create the dump-to file", r);

    // Contents must contain the original `int main` token, otherwise
    // the Rewriter serialized something unexpected.
    std::ifstream in(dump, std::ios::binary);
    std::ostringstream oss;
    oss << in.rdbuf();
    const std::string body = oss.str();
    CHECK(contains(body, "int main"));
    // PM1-7: the plugin's on-disk dump prepends the same two-line
    // `// AUTO-GENERATED by sturm-transpile ...` idempotency header
    // the standalone driver emits so a round-trip through the standalone
    // binary is byte-identical. That contract change is what unblocks
    // the `transpiler_idempotent_example_*` CTests in plugin mode and
    // the `test_cmake_glue_artifacts` sentinel check.  The in-memory
    // buffer fed to the nested CompilerInvocation is still header-free
    // (the header is a build-system-observability concern, not a
    // codegen input) — that invariant is exercised by test_plugin_nested.
    CHECK(contains(body, "AUTO-GENERATED"));
}

// Gate 4 — unknown plugin args are silently ignored (future-compat).
void gate_unknown_arg_ignored(const std::string& dir) {
    const std::string src = dir + "/unknown.cpp";
    write_file(src, "int main() { return 0; }\n");

    const std::string obj = dir + "/unknown.o";
    std::string cmd = std::string(clangxx_bin()) +
        plugin_active_args() +
        plugin_arg_pair("future-flag-42") +
        " -c -o " + obj +
        " " + src;
    auto r = run(cmd);
    CHECK_MSG(r.exit_code == 0,
              "plugin load failed on unknown future flag", r);
}

// Gate 5 — empty `dump-to=` is rejected (ParseArgs returns false).
// Clang surfaces that as a plugin load failure; we assert non-zero
// exit so the contract is pinned.
void gate_empty_dump_to_rejected(const std::string& dir) {
    const std::string src = dir + "/reject.cpp";
    write_file(src, "int main() { return 0; }\n");

    const std::string obj = dir + "/reject.o";
    std::string cmd = std::string(clangxx_bin()) +
        plugin_active_args() +
        plugin_arg_pair("dump-to=") +
        " -c -o " + obj +
        " " + src;
    auto r = run(cmd);
    CHECK_MSG(r.exit_code != 0,
              "plugin accepted empty dump-to= path", r);
}

// Gate 6 — PM4-4 `load=<abs-path-to-demo-plugin>` succeeds.
// Loading the PM4-7 demo plugin (MODULE library built in-tree) through
// the host transpiler plugin's `ParseArgs` must:
//   - Complete compilation with exit code 0.
//   - Under `verbose`, emit the `loaded runtime plugin` trace line so the
//     test can observe that the dlopen + version + dlsym chain reached
//     the end-of-success branch.
//   - Under `verbose`, list the `pm4.demo.tag` kind_id in the
//     `registered kind_ids:` trace line. This is the "kind_id is
//     resolvable afterward" gate the PM4-4 issue calls out: the plugin
//     entry executed, `register_op("pm4.demo.tag", ...)` populated a
//     probe Registry, and the host observed the kind via
//     `Registry::kind_ids()`.
//   - NOT surface any `error:` / `fatal error` / `failed to dlopen`
//     diagnostic.
// The gate pins the happy-path end-to-end: the demo plugin compiled
// against the same Clang the host was compiled against MUST load.
void gate_load_demo_plugin_succeeds(const std::string& dir) {
    const std::string src = dir + "/load_demo.cpp";
    write_file(src, "int main() { return 0; }\n");

    const std::string obj = dir + "/load_demo.o";
    std::string cmd = std::string(clangxx_bin()) +
        plugin_active_args() +
        plugin_arg_pair("verbose") +
        plugin_arg_pair(std::string("load=") + demo_plugin_path()) +
        " -c -o " + obj +
        " " + src;
    auto r = run(cmd);
    CHECK_MSG(r.exit_code == 0,
              "plugin load=<demo plugin> compile failed", r);
    CHECK_MSG(contains(r.combined_output,
                       "loaded runtime plugin"),
              "verbose trace did not report runtime plugin load", r);
    CHECK_MSG(contains(r.combined_output, demo_plugin_path()),
              "verbose trace missing demo plugin path", r);
    CHECK_MSG(contains(r.combined_output, "registered kind_ids:"),
              "verbose trace missing 'registered kind_ids' line", r);
    CHECK_MSG(contains(r.combined_output, "pm4.demo.tag"),
              "verbose trace did not list pm4.demo.tag kind_id", r);
    CHECK_MSG(!contains(r.combined_output, "failed to dlopen"),
              "plugin reported dlopen failure on valid path", r);
    CHECK_MSG(!contains(r.combined_output, "error:"),
              "plugin load produced an error diagnostic", r);
}

// Gate 7 — `load=/nonexistent/path.so` fails with the documented
// `failed to dlopen` error. The plan §3 pins this exact stderr string
// (PM4-9 `pm4_missing_plugin_errors` will grep for it); we assert the
// contract here so a future plugin.cpp refactor cannot silently change
// the wording.
void gate_load_nonexistent_errors(const std::string& dir) {
    const std::string src = dir + "/load_missing.cpp";
    write_file(src, "int main() { return 0; }\n");

    const std::string obj = dir + "/load_missing.o";
    // The path is intentionally nonsensical to maximise the chance that
    // no file of that name exists on any CI host. A real path ending in
    // ".does-not-exist-xyz.so" under /tmp also works, but the /proc
    // prefix avoids a tempdir-cleanup race if an unrelated test left a
    // like-named artifact behind.
    const std::string bogus =
        "/proc/self/nonexistent-sturm-pm4-plugin.so";
    std::string cmd = std::string(clangxx_bin()) +
        plugin_active_args() +
        plugin_arg_pair(std::string("load=") + bogus) +
        " -c -o " + obj +
        " " + src;
    auto r = run(cmd);
    CHECK_MSG(r.exit_code != 0,
              "plugin accepted nonexistent load= path", r);
    CHECK_MSG(contains(r.combined_output, "failed to dlopen"),
              "plugin did not report dlopen failure for missing plugin",
              r);
}

// Gate 8 — empty `load=` is rejected (same posture as empty `dump-to=`).
// A typo in the CMake glue (e.g. `PLUGINS ""`) must surface as a hard
// parse error rather than silently disabling the plugin load.
void gate_empty_load_rejected(const std::string& dir) {
    const std::string src = dir + "/load_empty.cpp";
    write_file(src, "int main() { return 0; }\n");

    const std::string obj = dir + "/load_empty.o";
    std::string cmd = std::string(clangxx_bin()) +
        plugin_active_args() +
        plugin_arg_pair("load=") +
        " -c -o " + obj +
        " " + src;
    auto r = run(cmd);
    CHECK_MSG(r.exit_code != 0,
              "plugin accepted empty load= path", r);
}

}  // namespace

int main() {
    std::string dir = tempdir();
    std::fprintf(stderr, "sturm-transpile plugin-load scratch: %s\n",
                 dir.c_str());
    std::fprintf(stderr, "plugin path:  %s\n", plugin_path());
    std::fprintf(stderr, "clang++ bin:  %s\n", clangxx_bin());

    gate_plugin_loads_on_non_sturm(dir);
    gate_verbose_flag_prints_trace(dir);
    gate_dump_to_flag_writes_file(dir);
    gate_unknown_arg_ignored(dir);
    gate_empty_dump_to_rejected(dir);
    // PM4-4 gates.
    gate_load_demo_plugin_succeeds(dir);
    gate_load_nonexistent_errors(dir);
    gate_empty_load_rejected(dir);

    std::fprintf(stderr, "\ntest_plugin_load: %d/%d passed\n",
                 tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
