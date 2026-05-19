// transpiler/tests/bucket_dispatchers/test_bucket_plugin_main.cpp
//
// sturm-r8xu: dispatcher main for `test_bucket_plugin`.
// Members:
//   test_plugin_demo
//   test_plugin_registry
//   test_pm4_dogfood_registry

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>

int run_test_plugin_demo(int argc, char** argv);
int run_test_plugin_registry(int argc, char** argv);
int run_test_pm4_dogfood_registry(int argc, char** argv);

int main(int argc, char** argv) {
    using Fn = int(*)(int, char**);
    static const std::unordered_map<std::string, Fn> kTests = {
        {"test_plugin_demo",         run_test_plugin_demo},
        {"test_plugin_registry",     run_test_plugin_registry},
        {"test_pm4_dogfood_registry",run_test_pm4_dogfood_registry},
    };
    if (argc < 2) {
        std::fprintf(stderr,
                     "test_bucket_plugin: usage: %s <test_name> [args...]\n"
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
                     "test_bucket_plugin: unknown test name '%s'\n",
                     argv[1]);
        return 2;
    }
    return it->second(argc - 1, argv + 1);
}
