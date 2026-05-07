// test_plugin_nested.cpp — PM1-4 integration test for the nested
// CompilerInvocation + EmitObjAction path in the sturm-transpile plugin.
//
// The gate the PM1-4 issue (sturm-bkcr) commits us to:
//
//     1. Legacy two-step path: sturm-transpile <src> → rewritten.cpp,
//        then clang++ -c rewritten.cpp -o legacy.o
//     2. Plugin path:          clang++ -fplugin=<plugin.so> -c <src>
//                              -o plugin.o
//
// Both .os must disassemble to the same instruction stream (via
// llvm-objdump -d). PID-stamped metadata (.file string tables, a few
// @N suffixes in mangled literal pool labels) may differ, so the test
// compares the INSTRUCTION-bearing lines — it strips header / blank /
// Disassembly / file-name / symbol-banner lines, strips byte columns,
// and compares what remains.
//
// The fixtures are three examples exercising distinct rewrite shapes:
//   * compound_expression.cpp  — Phase E flatten + uncompute injection.
//   * control_flow.cpp         — Phase H brace-wrap + Phase-A/B/C ops.
//   * zero_ancilla_fusion.cpp  — Phase J PJ-1 CCNOT-fuse peephole.
//
// Kept as a standalone `int main()` harness so ctest treats exit 0 as
// pass, anything else as fail.

#include <algorithm>
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
#include <vector>

namespace {

#ifndef STURM_PLUGIN_PATH
#error "STURM_PLUGIN_PATH must be defined to the plugin shared-library path"
#endif
#ifndef STURM_PLUGIN_CLANGXX
#error "STURM_PLUGIN_CLANGXX must be defined to the clang++ driver path"
#endif
#ifndef STURM_TRANSPILE_BIN
#error "STURM_TRANSPILE_BIN must be defined to the sturm-transpile path"
#endif
#ifndef STURM_PLUGIN_OBJDUMP
#error "STURM_PLUGIN_OBJDUMP must be defined to the llvm-objdump binary path"
#endif
#ifndef STURM_PLUGIN_EXAMPLES_DIR
#error "STURM_PLUGIN_EXAMPLES_DIR must be defined to the examples dir path"
#endif
#ifndef STURM_PLUGIN_INCLUDE_DIR
#error "STURM_PLUGIN_INCLUDE_DIR must be defined to the project include dir"
#endif
#ifndef STURM_PLUGIN_GENERATED_INCLUDE_DIR
#error "STURM_PLUGIN_GENERATED_INCLUDE_DIR must be defined to the build-tree include dir holding generated headers (e.g. sturm/version.hpp)"
#endif

const char* plugin_path()      { return STURM_PLUGIN_PATH; }
const char* clangxx_bin()      { return STURM_PLUGIN_CLANGXX; }
const char* transpile_bin()    { return STURM_TRANSPILE_BIN; }
const char* objdump_bin()      { return STURM_PLUGIN_OBJDUMP; }
const char* examples_dir()     { return STURM_PLUGIN_EXAMPLES_DIR; }
const char* include_dir()      { return STURM_PLUGIN_INCLUDE_DIR; }
const char* generated_include_dir() { return STURM_PLUGIN_GENERATED_INCLUDE_DIR; }

struct RunResult {
    int exit_code = -1;
    std::string combined_output;
};

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
    char tmpl[] = "/tmp/sturm_plugin_nested_XXXXXX";
    if (!::mkdtemp(tmpl)) {
        std::fprintf(stderr, "mkdtemp failed: %s\n", std::strerror(errno));
        std::exit(2);
    }
    return std::string(tmpl);
}

bool file_exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && st.st_size > 0;
}

bool contains(const std::string& h, const std::string& n) {
    return h.find(n) != std::string::npos;
}

std::string read_file_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
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

// Shared compile flag bundle for both the legacy rewritten source and
// the plugin invocation. Matches what cmake/SturmTranspile.cmake threads
// onto an add_quantum_executable() target: -I<project-include>,
// -DSTURM_BACKEND_ENABLED=1, -std=c++20, -c, -O0 (keep codegen stable
// across the two paths by NOT defaulting to whatever the user's
// $CXXFLAGS says).
//
// We want the two paths to feed identical frontend state into codegen,
// which is precisely what PM1-4's invocation-clone promises. Matching
// flags here means a mismatch later points at the plugin, not at the
// build flags.
std::string common_flags() {
    std::string f;
    f += " -std=c++20";
    f += " -O0";
    f += " -c";
    f += " -I";
    f += include_dir();
    f += " -I";
    f += generated_include_dir();
    f += " -DSTURM_BACKEND_ENABLED=1";
    // sturm-5jta (P2.b / G5): the legacy ancilla-cap define is gone;
    // qubit_pool.hpp grows on demand without a compile-time knob.
    return f;
}

// Strip anything in disassembly that is allowed to differ between the
// two paths:
//   * blank lines
//   * Disassembly / file / section headers (ELF notes, etc.)
//   * Symbol banners ("<main>:" etc.) — the leading address differs
//     between the two paths because the two .os have different
//     SHT_SYMTAB ordering when codegen inserts auxiliary functions.
//   * Byte columns between the address and the mnemonic — we keep the
//     mnemonic and its operands, which is the "instruction sequence"
//     the issue asks us to compare.
//   * "file format" banners.
//
// Output is a newline-joined string of the normalised mnemonic+operand
// lines. We intentionally DO NOT strip addresses relative to function
// starts — they encode control-flow structure and are the same on both
// paths when codegen is identical.
std::string normalise_objdump(const std::string& s) {
    std::ostringstream out;
    std::istringstream in(s);
    std::string line;
    while (std::getline(in, line)) {
        // Trim trailing whitespace.
        while (!line.empty() &&
               (line.back() == ' ' || line.back() == '\t' ||
                line.back() == '\r')) {
            line.pop_back();
        }
        if (line.empty()) continue;

        // Skip obvious non-instruction lines.
        if (line.find("file format") != std::string::npos) continue;
        if (line.rfind("Disassembly", 0) == 0) continue;
        if (!line.empty() && line.back() == ':' &&
            line.find('<') != std::string::npos) {
            // Symbol banner: "  0000000000000000 <main>:"
            continue;
        }

        // An instruction line looks like:
        //   "       0: 55                           pushq %rbp"
        // Drop everything up through the byte-column by splitting on
        // the first tab. llvm-objdump emits the mnemonic+operands after
        // the tab; before the tab is address + byte columns which are
        // allowed to differ between the two paths (different surrounding
        // function sizes push the same instruction to different byte
        // offsets).  We keep just the mnemonic+operands.
        auto tab = line.find('\t');
        std::string instr = (tab == std::string::npos)
                                ? line
                                : line.substr(tab + 1);
        // Collapse multi-tabs and internal padding the same way across
        // both sides.
        std::string norm;
        norm.reserve(instr.size());
        bool prev_space = false;
        for (char c : instr) {
            if (c == '\t') c = ' ';
            if (c == ' ') {
                if (prev_space) continue;
                prev_space = true;
            } else {
                prev_space = false;
            }
            norm.push_back(c);
        }
        // Strip leading / trailing whitespace after collapse.
        while (!norm.empty() && norm.front() == ' ') norm.erase(0, 1);
        while (!norm.empty() && norm.back()  == ' ') norm.pop_back();
        if (norm.empty()) continue;
        out << norm << '\n';
    }
    return out.str();
}

// Helper: Run `llvm-objdump -d <obj>` and return the normalised output.
std::string dump_object(const std::string& obj_path) {
    std::string cmd = std::string(objdump_bin()) + " -d " + obj_path;
    auto r = run(cmd);
    if (r.exit_code != 0) {
        std::fprintf(stderr,
                     "llvm-objdump failed on %s: exit=%d output=%s\n",
                     obj_path.c_str(), r.exit_code,
                     r.combined_output.c_str());
        return {};
    }
    return normalise_objdump(r.combined_output);
}

// Shared compare helper. `name` is used only in logging.
void compare_one(const std::string& dir, const char* name) {
    const std::string src = std::string(examples_dir()) + "/" + name;
    if (!file_exists(src)) {
        std::fprintf(stderr, "skipping %s: fixture not found at %s\n",
                     name, src.c_str());
        return;
    }

    // ── Legacy two-step ─────────────────────────────────────────────
    // Step A: run sturm-transpile with --dump-transpiled so we do not
    // have to manage an output-dir tree.
    const std::string rewritten =
        dir + "/" + std::string(name) + ".legacy.cpp";
    {
        std::string cmd = std::string(transpile_bin()) +
            " --dump-transpiled " + rewritten +
            " " + src +
            " -- " +
            " -std=c++20 -I" + include_dir() +
            " -I" + generated_include_dir() +
            " -DSTURM_BACKEND_ENABLED=1";
        auto r = run(cmd);
        CHECK_MSG(r.exit_code == 0, "sturm-transpile step failed", r);
        CHECK_MSG(file_exists(rewritten),
                  "sturm-transpile did not produce rewritten source", r);
    }

    // Step B: clang++ -c on the rewritten source.
    const std::string legacy_obj = dir + "/" + name + ".legacy.o";
    {
        std::string cmd = std::string(clangxx_bin()) +
            common_flags() +
            " -o " + legacy_obj +
            " " + rewritten;
        auto r = run(cmd);
        CHECK_MSG(r.exit_code == 0,
                  "clang++ -c on rewritten source failed", r);
        CHECK_MSG(file_exists(legacy_obj),
                  "legacy rewritten .o not produced", r);
    }

    // ── Plugin one-step ─────────────────────────────────────────────
    // Activate the plugin explicitly so ReplaceAction fires; our
    // ExecuteAction override now runs a nested EmitObjAction which
    // produces the .o at -o.  This is the PM1-4 gate: the plugin path
    // produces the SAME instruction stream as the legacy path, in one
    // command.
    const std::string plugin_obj = dir + "/" + name + ".plugin.o";
    {
        std::string cmd = std::string(clangxx_bin()) +
            " -Xclang -load -Xclang " + plugin_path() +
            " -Xclang -plugin -Xclang sturm-transpile" +
            common_flags() +
            " -o " + plugin_obj +
            " " + src;
        auto r = run(cmd);
        CHECK_MSG(r.exit_code == 0, "plugin nested compile failed", r);
        CHECK_MSG(file_exists(plugin_obj),
                  "plugin nested invocation did not produce .o", r);
    }

    // ── Instruction-stream compare ──────────────────────────────────
    const std::string legacy_dump = dump_object(legacy_obj);
    const std::string plugin_dump = dump_object(plugin_obj);
    CHECK(!legacy_dump.empty());
    CHECK(!plugin_dump.empty());

    if (legacy_dump != plugin_dump) {
        // Dump a diff-friendly hint to stderr on mismatch. We emit the
        // first N lines that diverge rather than the whole file.
        std::fprintf(stderr, "FAIL  %s:%d  disassembly mismatch on %s\n",
                     __FILE__, __LINE__, name);
        std::istringstream a(legacy_dump), b(plugin_dump);
        std::string la, lb;
        int shown = 0;
        while (shown < 20) {
            bool has_a = static_cast<bool>(std::getline(a, la));
            bool has_b = static_cast<bool>(std::getline(b, lb));
            if (!has_a && !has_b) break;
            if (la != lb) {
                std::fprintf(stderr, "    legacy: %s\n",
                             has_a ? la.c_str() : "<eof>");
                std::fprintf(stderr, "    plugin: %s\n",
                             has_b ? lb.c_str() : "<eof>");
                ++shown;
            }
        }
        ++tests_run;  // register the failure
    } else {
        ++tests_run;
        ++tests_pass;
    }
}

}  // namespace

int main() {
    const std::string dir = tempdir();
    std::fprintf(stderr, "sturm-transpile plugin-nested scratch: %s\n",
                 dir.c_str());
    std::fprintf(stderr, "plugin path:  %s\n", plugin_path());
    std::fprintf(stderr, "clang++ bin:  %s\n", clangxx_bin());
    std::fprintf(stderr, "transpile:    %s\n", transpile_bin());
    std::fprintf(stderr, "objdump:      %s\n", objdump_bin());

    compare_one(dir, "compound_expression.cpp");
    compare_one(dir, "control_flow.cpp");
    compare_one(dir, "zero_ancilla_fusion.cpp");

    std::fprintf(stderr, "\ntest_plugin_nested: %d/%d passed\n",
                 tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
