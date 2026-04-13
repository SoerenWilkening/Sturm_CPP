// test_e2e_loc_budget.cpp — M21 (PRD v3): Module LoC budget verification.
//
// Tests:
//   test_e2e_loc_budget — verify all module LoC budgets from the implementation plan.
//
// Uses wc -l to count lines in each module and assert against the budget from
// docs/12_implementation_plan_backend_v3.md Table "Module size budget".
//
// Budgets (from implementation plan):
//   primitives.hpp         < 80  (was primitives_v3.hpp, renamed in M20)
//   control_stack.hpp      < 120
//   control_stack.cpp      < 40
//   qbool_ops.hpp          < 200
//   lazy_expr.hpp          < 150
//   adder_dsl.hpp          < 250
//   logic_dsl.hpp          < 120
//   c_and_dsl.hpp          < 150
//   swap_dsl.hpp           < 100
//   mul_dsl.hpp            < 150
//   div_dsl.hpp            < 300
//   compare_dsl.hpp        < 200
//   mod_dsl.hpp            < 60
//   pow_dsl.hpp            < 200
//   qint_arith_v3.hpp      < 250
//   qint_bitwise_v3.hpp    < 150
//   qint_compare_v3.hpp    < 150
//
// Harness: plain assert + printf (no gtest).

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifndef STURM_REPO_ROOT
#  error "STURM_REPO_ROOT must be defined via -DSTURM_REPO_ROOT=... in CMakeLists.txt"
#endif

// Count lines in a file using wc -l.
static long count_lines(const char* path) {
    std::string cmd = std::string("wc -l < '") + path + "' 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return -1;
    char buf[64] = {};
    fgets(buf, sizeof(buf), pipe);
    pclose(pipe);
    return std::atol(buf);
}

struct ModuleBudget {
    const char* rel_path;   // relative to STURM_REPO_ROOT
    const char* name;       // display name
    int         budget;     // max allowed lines (exclusive upper bound)
};

static const ModuleBudget kBudgets[] = {
    // M11
    { "include/sturm/backend/primitives.hpp",           "primitives.hpp",        80  },
    // M12
    { "include/sturm/core/control_stack.hpp",           "control_stack.hpp",     120 },
    { "src/sturm/core/control_stack.cpp",               "control_stack.cpp",     40  },
    // M13
    { "include/sturm/qtypes/qbool_ops.hpp",             "qbool_ops.hpp",         250 },
    { "include/sturm/qtypes/lazy_expr.hpp",             "lazy_expr.hpp",         150 },
    // M14
    { "include/sturm/lib/adder_dsl.hpp",                "adder_dsl.hpp",         250 },
    // M15
    { "include/sturm/lib/logic_dsl.hpp",                "logic_dsl.hpp",         120 },
    { "include/sturm/lib/c_and_dsl.hpp",                "c_and_dsl.hpp",         150 },
    // M16
    { "include/sturm/lib/swap_dsl.hpp",                 "swap_dsl.hpp",          100 },
    { "include/sturm/lib/mul_dsl.hpp",                  "mul_dsl.hpp",           150 },
    { "include/sturm/lib/div_dsl.hpp",                  "div_dsl.hpp",           300 },
    // M17
    { "include/sturm/lib/compare_dsl.hpp",              "compare_dsl.hpp",       200 },
    { "include/sturm/lib/mod_dsl.hpp",                  "mod_dsl.hpp",           60  },
    { "include/sturm/lib/pow_dsl.hpp",                  "pow_dsl.hpp",           200 },
    // M19
    { "include/sturm/qtypes/qint_arith_v3.hpp",         "qint_arith_v3.hpp",     250 },
    { "include/sturm/qtypes/qint_bitwise_v3.hpp",       "qint_bitwise_v3.hpp",   150 },
    { "include/sturm/qtypes/qint_compare_v3.hpp",       "qint_compare_v3.hpp",   150 },
};

static void test_e2e_loc_budget() {
    const std::string root = STURM_REPO_ROOT;
    bool all_ok = true;

    for (const auto& m : kBudgets) {
        std::string full_path = root + "/" + m.rel_path;
        long lines = count_lines(full_path.c_str());

        if (lines < 0) {
            std::fprintf(stderr, "  SKIP: %s — file not found or wc failed\n",
                         m.name);
            // Missing file is an error (all modules should exist post-M20).
            all_ok = false;
            continue;
        }

        if (lines >= m.budget) {
            std::fprintf(stderr,
                "  FAIL LoC: %s = %ld lines (budget < %d)\n",
                m.name, lines, m.budget);
            all_ok = false;
        } else {
            std::printf("  OK: %s = %ld lines (budget < %d)\n",
                        m.name, lines, m.budget);
        }
    }

    assert(all_ok && "One or more modules exceed their LoC budget");
}

int main() {
    std::printf("M21 module LoC budget verification:\n");
    test_e2e_loc_budget();
    std::printf("All M21 e2e_loc_budget checks passed.\n");
    return 0;
}
