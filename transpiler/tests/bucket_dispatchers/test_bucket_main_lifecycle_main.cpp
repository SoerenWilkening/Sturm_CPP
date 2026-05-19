// transpiler/tests/bucket_dispatchers/test_bucket_main_lifecycle_main.cpp
//
// sturm-r8xu: dispatcher main for `test_bucket_main_lifecycle`.
// Members:
//   test_matcher_main_lifecycle_entry_point
//   test_main_lifecycle_emitter_entry_point

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>

int run_test_matcher_main_lifecycle_entry_point(int argc, char** argv);
int run_test_main_lifecycle_emitter_entry_point(int argc, char** argv);

int main(int argc, char** argv) {
    using Fn = int(*)(int, char**);
    static const std::unordered_map<std::string, Fn> kTests = {
        {"test_matcher_main_lifecycle_entry_point",
         run_test_matcher_main_lifecycle_entry_point},
        {"test_main_lifecycle_emitter_entry_point",
         run_test_main_lifecycle_emitter_entry_point},
    };
    if (argc < 2) {
        std::fprintf(stderr,
                     "test_bucket_main_lifecycle: usage: %s <test_name> [args...]\n"
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
                     "test_bucket_main_lifecycle: unknown test name '%s'\n",
                     argv[1]);
        return 2;
    }
    return it->second(argc - 1, argv + 1);
}
