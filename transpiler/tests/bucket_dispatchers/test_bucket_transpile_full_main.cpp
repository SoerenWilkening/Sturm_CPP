// transpiler/tests/bucket_dispatchers/test_bucket_transpile_full_main.cpp
//
// sturm-r8xu: dispatcher main for `test_bucket_transpile_full`.
// Members:
//   test_transpile_matcher
//   test_transpile_consumer

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>

int run_test_transpile_matcher(int argc, char** argv);
int run_test_transpile_consumer(int argc, char** argv);

int main(int argc, char** argv) {
    using Fn = int(*)(int, char**);
    static const std::unordered_map<std::string, Fn> kTests = {
        {"test_transpile_matcher",  run_test_transpile_matcher},
        {"test_transpile_consumer", run_test_transpile_consumer},
    };
    if (argc < 2) {
        std::fprintf(stderr,
                     "test_bucket_transpile_full: usage: %s <test_name> [args...]\n"
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
                     "test_bucket_transpile_full: unknown test name '%s'\n",
                     argv[1]);
        return 2;
    }
    return it->second(argc - 1, argv + 1);
}
