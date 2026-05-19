// transpiler/tests/bucket_dispatchers/test_bucket_pipeline_main.cpp
//
// sturm-r8xu: dispatcher main for `test_bucket_pipeline`.
// Members:
//   test_transpile_emitter
//   test_transpile_uncompute_pass
//   test_alias_footprint
//   test_width_inference
//   test_transpile_diag_context
//   test_qint_implicit_warn
//   test_matcher_peephole_reorder

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>

int run_test_transpile_emitter(int argc, char** argv);
int run_test_transpile_uncompute_pass(int argc, char** argv);
int run_test_alias_footprint(int argc, char** argv);
int run_test_width_inference(int argc, char** argv);
int run_test_transpile_diag_context(int argc, char** argv);
int run_test_qint_implicit_warn(int argc, char** argv);
int run_test_matcher_peephole_reorder(int argc, char** argv);

int main(int argc, char** argv) {
    using Fn = int(*)(int, char**);
    static const std::unordered_map<std::string, Fn> kTests = {
        {"test_transpile_emitter",        run_test_transpile_emitter},
        {"test_transpile_uncompute_pass", run_test_transpile_uncompute_pass},
        {"test_alias_footprint",          run_test_alias_footprint},
        {"test_width_inference",          run_test_width_inference},
        {"test_transpile_diag_context",   run_test_transpile_diag_context},
        {"test_qint_implicit_warn",       run_test_qint_implicit_warn},
        {"test_matcher_peephole_reorder", run_test_matcher_peephole_reorder},
    };
    if (argc < 2) {
        std::fprintf(stderr,
                     "test_bucket_pipeline: usage: %s <test_name> [args...]\n"
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
                     "test_bucket_pipeline: unknown test name '%s'\n",
                     argv[1]);
        return 2;
    }
    return it->second(argc - 1, argv + 1);
}
