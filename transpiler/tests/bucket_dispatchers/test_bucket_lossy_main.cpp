// transpiler/tests/bucket_dispatchers/test_bucket_lossy_main.cpp
//
// sturm-r8xu: dispatcher main for `test_bucket_lossy`.
// Members:
//   test_matcher_lossy_op
//   test_lossy_rewrite_emitter
//   test_lossy_scope_exit_emitter
//   test_lossy_nested_rewrite

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>

int run_test_matcher_lossy_op(int argc, char** argv);
int run_test_lossy_rewrite_emitter(int argc, char** argv);
int run_test_lossy_scope_exit_emitter(int argc, char** argv);
int run_test_lossy_nested_rewrite(int argc, char** argv);

int main(int argc, char** argv) {
    using Fn = int(*)(int, char**);
    static const std::unordered_map<std::string, Fn> kTests = {
        {"test_matcher_lossy_op",          run_test_matcher_lossy_op},
        {"test_lossy_rewrite_emitter",     run_test_lossy_rewrite_emitter},
        {"test_lossy_scope_exit_emitter",  run_test_lossy_scope_exit_emitter},
        {"test_lossy_nested_rewrite",      run_test_lossy_nested_rewrite},
    };
    if (argc < 2) {
        std::fprintf(stderr,
                     "test_bucket_lossy: usage: %s <test_name> [args...]\n"
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
                     "test_bucket_lossy: unknown test name '%s'\n",
                     argv[1]);
        return 2;
    }
    return it->second(argc - 1, argv + 1);
}
