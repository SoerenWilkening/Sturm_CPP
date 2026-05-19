// transpiler/tests/bucket_dispatchers/test_bucket_qram_main.cpp
//
// sturm-r8xu: dispatcher main for the `test_bucket_qram` test executable.
// Each member test's `int main()` has been renamed to
// `int run_<test_name>(int argc, char** argv)`. This dispatcher selects the
// right `run_*` entry point by name (`argv[1]`) so each logical ctest entry
// still maps to a single member test, but they all share one link target.
//
// Members:
//   test_matcher_qram_subscript
//   test_matcher_qram_subscript_assign
//   test_matcher_qram_subscript_expr
//   test_qram_emitter
//   test_qram_emitter_assign
//   test_qram_emitter_expr
//   test_matcher_qram_oos
//   test_qram_e2e
//   test_qram_e2e_real

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <unordered_map>

int run_test_matcher_qram_subscript(int argc, char** argv);
int run_test_matcher_qram_subscript_assign(int argc, char** argv);
int run_test_matcher_qram_subscript_expr(int argc, char** argv);
int run_test_qram_emitter(int argc, char** argv);
int run_test_qram_emitter_assign(int argc, char** argv);
int run_test_qram_emitter_expr(int argc, char** argv);
int run_test_matcher_qram_oos(int argc, char** argv);
int run_test_qram_e2e(int argc, char** argv);
int run_test_qram_e2e_real(int argc, char** argv);

int main(int argc, char** argv) {
    using Fn = int(*)(int, char**);
    static const std::unordered_map<std::string, Fn> kTests = {
        {"test_matcher_qram_subscript",        run_test_matcher_qram_subscript},
        {"test_matcher_qram_subscript_assign", run_test_matcher_qram_subscript_assign},
        {"test_matcher_qram_subscript_expr",   run_test_matcher_qram_subscript_expr},
        {"test_qram_emitter",                  run_test_qram_emitter},
        {"test_qram_emitter_assign",           run_test_qram_emitter_assign},
        {"test_qram_emitter_expr",             run_test_qram_emitter_expr},
        {"test_matcher_qram_oos",              run_test_matcher_qram_oos},
        {"test_qram_e2e",                      run_test_qram_e2e},
        {"test_qram_e2e_real",                 run_test_qram_e2e_real},
    };
    if (argc < 2) {
        std::fprintf(stderr,
                     "test_bucket_qram: usage: %s <test_name> [args...]\n"
                     "available tests:\n",
                     argv[0]);
        for (const auto& kv : kTests) {
            std::fprintf(stderr, "  %s\n", kv.first.c_str());
        }
        return 2;
    }
    auto it = kTests.find(argv[1]);
    if (it == kTests.end()) {
        std::fprintf(stderr,
                     "test_bucket_qram: unknown test name '%s'\n",
                     argv[1]);
        return 2;
    }
    return it->second(argc - 1, argv + 1);
}
