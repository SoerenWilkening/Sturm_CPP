// transpiler/tests/bucket_dispatchers/test_bucket_reversible_main.cpp
//
// sturm-r8xu: dispatcher main for the `test_bucket_reversible` test
// executable. Each member's `int main()` was renamed to
// `int run_<test_name>(int argc, char** argv)`; this dispatcher routes
// `argv[1]` to the right entry point.
//
// Members:
//   test_matcher_reversible_drive
//   test_matcher_reversible_validate
//   test_matcher_reversible_signature
//   test_synthesis_registry
//   test_return_to_out_param
//   test_adjoint_emitter
//   test_auto_register_emitter
//   test_reversible_attribute
//   test_loop_reversal
//   test_entry_point_attribute

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <unordered_map>

int run_test_matcher_reversible_drive(int argc, char** argv);
int run_test_matcher_reversible_validate(int argc, char** argv);
int run_test_matcher_reversible_signature(int argc, char** argv);
int run_test_synthesis_registry(int argc, char** argv);
int run_test_return_to_out_param(int argc, char** argv);
int run_test_adjoint_emitter(int argc, char** argv);
int run_test_auto_register_emitter(int argc, char** argv);
int run_test_reversible_attribute(int argc, char** argv);
int run_test_loop_reversal(int argc, char** argv);
int run_test_entry_point_attribute(int argc, char** argv);

int main(int argc, char** argv) {
    using Fn = int(*)(int, char**);
    static const std::unordered_map<std::string, Fn> kTests = {
        {"test_matcher_reversible_drive",    run_test_matcher_reversible_drive},
        {"test_matcher_reversible_validate", run_test_matcher_reversible_validate},
        {"test_matcher_reversible_signature",run_test_matcher_reversible_signature},
        {"test_synthesis_registry",          run_test_synthesis_registry},
        {"test_return_to_out_param",         run_test_return_to_out_param},
        {"test_adjoint_emitter",             run_test_adjoint_emitter},
        {"test_auto_register_emitter",       run_test_auto_register_emitter},
        {"test_reversible_attribute",        run_test_reversible_attribute},
        {"test_loop_reversal",               run_test_loop_reversal},
        {"test_entry_point_attribute",       run_test_entry_point_attribute},
    };
    if (argc < 2) {
        std::fprintf(stderr,
                     "test_bucket_reversible: usage: %s <test_name> [args...]\n"
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
                     "test_bucket_reversible: unknown test name '%s'\n",
                     argv[1]);
        return 2;
    }
    return it->second(argc - 1, argv + 1);
}
