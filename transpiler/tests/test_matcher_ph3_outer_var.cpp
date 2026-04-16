// test_matcher_ph3_outer_var.cpp — Phase H PH-3: outer-variable-mutation guard.
//
// The PH-3 matcher flags compound-assign mutations whose target qbool/qint
// is declared in an outer scope relative to the mutation site AND whose
// mutation lives inside a for/while/if/else/WHEN body. For every such
// hit the matcher:
//   (a) marks the matching QOperation with `skip_uncompute=true` so the
//       M8 synthesis pass emits no inverse, and
//   (b) prints a stderr diagnostic pointing at the mutation's line+col.
//
// Tests exercise the full pipeline via the production xor_assign matchers
// (Phase A PA-3/PA-4) layered with the PH-3 guard registered LAST — the
// ordering invariant described in `matcher.hpp` and enforced in `main.cpp`.

#include "test_matcher_harness.hpp"

#include "sturm/transpile/matcher.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace sturm::transpile;

namespace {

// Minimal qbool with `operator^=` (classical-RHS and qbool-RHS overloads)
// so PA-3/PA-4 match. WHEN is defined as a thin `if` macro whose
// expansion spells the name WHEN — the PH-3 classifier treats
// macro-expanded IfStmts exactly like user-written ifs.
constexpr std::string_view kQBoolXorAssignStub = R"CPP(
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
    qbool& operator^=(int) { return *this; }
};

} // namespace sturm
using sturm::qbool;

#define WHEN(cond) if (bool _when_val_ = (bool)(cond); _when_val_)
)CPP";

struct PH3Run {
    QUnit unit;
    int detections = 0;
};

PH3Run run_ph3_xor_assign_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kQBoolXorAssignStub.size() + user_src.size());
    code.append(kQBoolXorAssignStub);
    code.append(user_src);

    PH3Run out;
    reset_outer_var_guard_detection_count_for_test();

    clang::ast_matchers::MatchFinder finder;
    // Phase A matchers first so they push QOperations onto
    // `unit.scopes` before the PH-3 callback classifies each mutation.
    // Registration order is load-bearing: MatchFinder invokes callbacks
    // in registration order for a given matched node.
    register_xor_assign_matcher(finder, out.unit);
    register_xor_assign_classical_matcher(finder, out.unit);
    register_outer_var_guard_matcher(finder, out.unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PH-3 xor_assign)\n");
    }
    out.detections = outer_var_guard_detection_count_for_test();
    return out;
}

void test_ph3_outer_xor_inside_for_is_flagged() {
    // `qbool a` declared in the function body; mutated inside the for
    // body. PH-3 classifies as OuterMutation, flags the op, emits the
    // diagnostic. Exactly one op with `skip_uncompute == true`.
    PH3Run r = run_ph3_xor_assign_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 3; ++i) {\n"
        "        a ^= b;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 1);
    std::size_t skip_count = 0;
    std::size_t total = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            ++total;
            if (op.skip_uncompute) ++skip_count;
        }
    }
    CHECK(total == 1);
    CHECK(skip_count == 1);
}

void test_ph3_outer_xor_inside_while_is_flagged() {
    PH3Run r = run_ph3_xor_assign_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    while (true) {\n"
        "        a ^= b;\n"
        "        break;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 1);
    std::size_t skip_count = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            if (op.skip_uncompute) ++skip_count;
        }
    }
    CHECK(skip_count == 1);
}

void test_ph3_outer_xor_inside_if_is_flagged() {
    PH3Run r = run_ph3_xor_assign_matcher(
        "void demo(qbool a, qbool b, bool cond) {\n"
        "    if (cond) {\n"
        "        a ^= b;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 1);
    std::size_t skip_count = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            if (op.skip_uncompute) ++skip_count;
        }
    }
    CHECK(skip_count == 1);
}

void test_ph3_outer_xor_inside_if_else_is_flagged() {
    PH3Run r = run_ph3_xor_assign_matcher(
        "void demo(qbool a, qbool b, bool cond) {\n"
        "    if (cond) { (void)a; } else { a ^= b; }\n"
        "}\n");
    CHECK(r.detections == 1);
    std::size_t skip_count = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            if (op.skip_uncompute) ++skip_count;
        }
    }
    CHECK(skip_count == 1);
}

void test_ph3_outer_xor_inside_when_is_flagged() {
    // WHEN barriers count the same as user-written if barriers.
    PH3Run r = run_ph3_xor_assign_matcher(
        "void demo(qbool a, qbool b, bool cond) {\n"
        "    WHEN(cond) {\n"
        "        a ^= b;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 1);
    std::size_t skip_count = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            if (op.skip_uncompute) ++skip_count;
        }
    }
    CHECK(skip_count == 1);
}

void test_ph3_local_xor_without_control_flow_is_not_flagged() {
    // Negative: a bare `a ^= b;` at the function body level is
    // LocalMutation — no for/while/if/WHEN barrier between declaration
    // and mutation. PH-3 must NOT flag it.
    PH3Run r = run_ph3_xor_assign_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    a ^= b;\n"
        "}\n");
    CHECK(r.detections == 0);
    std::size_t skip_count = 0;
    std::size_t total = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            ++total;
            if (op.skip_uncompute) ++skip_count;
        }
    }
    CHECK(total == 1);
    CHECK(skip_count == 0);
}

void test_ph3_local_xor_in_for_of_locally_declared_var_is_not_flagged() {
    // Negative: `qbool r` declared inside the for-body block, then `r ^= b;`
    // right after — the mutation target lives in the SAME scope as the
    // mutation. PH-3 walks up from the `^=` and hits the CompoundStmt
    // (declaring scope) BEFORE any control-flow barrier → LocalMutation.
    PH3Run r = run_ph3_xor_assign_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 3; ++i) {\n"
        "        qbool r;\n"
        "        r ^= a;\n"
        "        (void)b;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 0);
    std::size_t skip_count = 0;
    std::size_t total = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            ++total;
            if (op.skip_uncompute) ++skip_count;
        }
    }
    CHECK(total == 1);
    CHECK(skip_count == 0);
}

void test_ph3_mixed_inner_intermediate_and_outer_mutation() {
    // Mixed scope — the key PH-3 criterion: "per-op flag granularity".
    // One scope contains:
    //   - `r ^= a;` where r is declared inside the for body
    //     (intermediate → skip_uncompute=false)
    //   - `a ^= b;` where a is declared outside the for
    //     (outer mutation → skip_uncompute=true)
    // Exactly ONE op carries the skip flag; detection counter bumps once.
    PH3Run r = run_ph3_xor_assign_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 3; ++i) {\n"
        "        qbool r;\n"
        "        r ^= a;\n"
        "        a ^= b;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 1);
    std::size_t skip_count = 0;
    std::size_t total = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            ++total;
            if (op.skip_uncompute) ++skip_count;
        }
    }
    CHECK(total == 2);
    CHECK(skip_count == 1);
}

} // namespace

void run_ph3_outer_var_tests() {
    test_ph3_outer_xor_inside_for_is_flagged();
    test_ph3_outer_xor_inside_while_is_flagged();
    test_ph3_outer_xor_inside_if_is_flagged();
    test_ph3_outer_xor_inside_if_else_is_flagged();
    test_ph3_outer_xor_inside_when_is_flagged();
    test_ph3_local_xor_without_control_flow_is_not_flagged();
    test_ph3_local_xor_in_for_of_locally_declared_var_is_not_flagged();
    test_ph3_mixed_inner_intermediate_and_outer_mutation();
}
