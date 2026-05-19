// test_routine_registry.cpp — Phase I PI-1 TDD tests for the routine
// registry and its AST-matcher-driven population.
//
// Contract (see bd sturm-mmsa + docs/roadmap_transpiler_post_mvp.md Phase I):
//   - RoutineRegistry owns a map const clang::FunctionDecl* -> std::string
//     where the key is the *forward* routine's FunctionDecl and the value
//     is the *adjoint*'s source-level identifier.
//   - The AST matcher fires on every expansion of
//         namespace sturm { namespace _detail {
//             template <> struct adjoint_of<decltype(&::fn)> {
//                 static constexpr auto value = &::adj;
//             };
//         }}
//     which is exactly what STURM_REGISTER_ADJOINT(fn, adj) expands to. On
//     match, the callback resolves the `fn` FunctionDecl from the template
//     argument's `decltype(&::fn)` and stores {fn_decl -> "adj"}.
//
// The tests build a C++ stub that declares a pair of free functions with
// compatible signatures, attaches the macro expansion verbatim (not via
// the macro — the tests control the expansion shape directly so they can
// pin behaviour without relying on the runtime header), and runs the
// PI-1 matcher through LibTooling's runToolOnCodeWithArgs. The tests
// then snapshot the registry contents (forward name + adjoint name) so
// the CHECKs can be evaluated after the tool's ASTContext is torn down
// without holding on to dangling FunctionDecl pointers.

#include "test_matcher_harness.hpp"

#include "routine_registry.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sturm_test_routine_registry_ns {

using namespace sturm::transpile;

namespace {

// Inline reproduction of the (already PI-0-shipped) macro expansion — we
// inline the expanded form rather than #including the runtime header so
// the test does not need the full `sturm::_detail::adjoint_of<>` primary
// to be visible (the matcher keys off the qualified name of the
// *specialization*, not the primary).
constexpr std::string_view kRoutineRegistryStub = R"CPP(
namespace sturm { namespace _detail {
template <typename FnPtr> struct adjoint_of;
}}

// Two forward/adjoint pairs — enough to verify that the matcher keys by
// FunctionDecl identity rather than by signature alone.
void fwd_a(int) {}
void adj_a(int) {}

int fwd_b(int x, int y) { return x + y; }
int adj_b(int x, int y) { return x - y; }

namespace sturm { namespace _detail {
template <> struct adjoint_of<decltype(&::fwd_a)> {
    static constexpr auto value = &::adj_a;
};
template <> struct adjoint_of<decltype(&::fwd_b)> {
    static constexpr auto value = &::adj_b;
};
}}
)CPP";

// Snapshot of the registry taken WHILE the ASTContext is still alive.
// Stores std::string pairs so assertions after tool teardown remain
// valid — the raw FunctionDecl* keys become dangling the moment the
// tool's ASTUnit is destroyed.
struct RegistrySnapshot {
    std::size_t size = 0;
    std::vector<std::pair<std::string, std::string>> pairs;  // {fwd, adj}
};

// One-shot ASTConsumer that runs the MatchFinder, then flattens the
// populated registry into a caller-owned RegistrySnapshot while the
// ASTContext is still alive.
class SnapshotConsumer : public clang::ASTConsumer {
public:
    SnapshotConsumer(RoutineRegistry* reg,
                     clang::ast_matchers::MatchFinder* finder,
                     RegistrySnapshot* out)
        : reg_(reg), finder_(finder), out_(out) {}
    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        finder_->matchAST(ctx);
        out_->size = reg_->size();
        for (const auto& entry : reg_->entries()) {
            const clang::FunctionDecl* fd = entry.first;
            out_->pairs.emplace_back(
                fd ? fd->getNameAsString() : std::string{"<null>"},
                entry.second);
        }
    }
private:
    RoutineRegistry* reg_;
    clang::ast_matchers::MatchFinder* finder_;
    RegistrySnapshot* out_;
};

class SnapshotAction : public clang::ASTFrontendAction {
public:
    SnapshotAction(RoutineRegistry* reg,
                   clang::ast_matchers::MatchFinder* finder,
                   RegistrySnapshot* out)
        : reg_(reg), finder_(finder), out_(out) {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<SnapshotConsumer>(reg_, finder_, out_);
    }
private:
    RoutineRegistry* reg_;
    clang::ast_matchers::MatchFinder* finder_;
    RegistrySnapshot* out_;
};

class SnapshotFactory : public clang::tooling::FrontendActionFactory {
public:
    SnapshotFactory(RoutineRegistry* reg,
                    clang::ast_matchers::MatchFinder* finder,
                    RegistrySnapshot* out)
        : reg_(reg), finder_(finder), out_(out) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<SnapshotAction>(reg_, finder_, out_);
    }
private:
    RoutineRegistry* reg_;
    clang::ast_matchers::MatchFinder* finder_;
    RegistrySnapshot* out_;
};

// Run the registry matcher over `kRoutineRegistryStub + user_src` and
// snapshot the registry before the tool tears down.
RegistrySnapshot run_registry_matcher(std::string_view user_src,
                                      bool prepend_stub = true) {
    std::string code;
    if (prepend_stub) {
        code.reserve(kRoutineRegistryStub.size() + user_src.size());
        code.append(kRoutineRegistryStub);
    }
    code.append(user_src);

    RegistrySnapshot out;
    RoutineRegistry reg;
    clang::ast_matchers::MatchFinder finder;
    register_routine_registry_matcher(finder, reg);

    SnapshotFactory factory(&reg, &finder, &out);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (routine_registry)\n");
    }
    return out;
}

// Locate the first pair in the result whose forward-name matches.
const std::pair<std::string, std::string>*
find_forward(const RegistrySnapshot& r, std::string_view name) {
    for (const auto& p : r.pairs) {
        if (p.first == name) return &p;
    }
    return nullptr;
}

void test_registry_api_basic_contract() {
    // Pure unit test of the RoutineRegistry helper — no LibTooling.
    // Covers the storage contract: empty by default, insert_pair
    // populates it, contains/size/lookup all reflect the inserts,
    // entries() iterates in insertion order, and clear() resets state.
    RoutineRegistry reg;
    CHECK(reg.empty());
    CHECK(reg.size() == 0);
    CHECK(!reg.contains(nullptr));
    CHECK(reg.lookup(nullptr) == nullptr);

    // Use a dummy non-null pointer value. We never dereference it — the
    // registry must not inspect the FunctionDecl, only use its identity
    // as a map key.
    auto* dummy_fn = reinterpret_cast<const clang::FunctionDecl*>(0x1);
    reg.insert_pair(dummy_fn, "adj_name");
    CHECK(!reg.empty());
    CHECK(reg.size() == 1);
    CHECK(reg.contains(dummy_fn));
    const std::string* found = reg.lookup(dummy_fn);
    CHECK(found != nullptr);
    if (found) CHECK_EQ_STR(*found, std::string("adj_name"));

    // Re-inserting the same key overwrites the value (last-write-wins
    // semantics, documented in the header). Two STURM_REGISTER_ADJOINT
    // expansions on the same forward are ill-formed at runtime
    // (specialization redefinition), but the registry helper stays
    // robust to avoid silently masking a parse-time error.
    reg.insert_pair(dummy_fn, "adj_other");
    CHECK(reg.size() == 1);
    found = reg.lookup(dummy_fn);
    CHECK(found != nullptr);
    if (found) CHECK_EQ_STR(*found, std::string("adj_other"));

    // entries() iterates in insertion order.
    auto* dummy_fn2 = reinterpret_cast<const clang::FunctionDecl*>(0x2);
    reg.insert_pair(dummy_fn2, "adj_two");
    auto entries = reg.entries();
    CHECK(entries.size() == 2);
    if (entries.size() == 2) {
        CHECK(entries[0].first == dummy_fn);
        CHECK_EQ_STR(entries[0].second, std::string("adj_other"));
        CHECK(entries[1].first == dummy_fn2);
        CHECK_EQ_STR(entries[1].second, std::string("adj_two"));
    }

    // insert_pair(nullptr, ...) is a no-op.
    reg.insert_pair(nullptr, "ignored");
    CHECK(reg.size() == 2);

    // clear() empties everything.
    reg.clear();
    CHECK(reg.empty());
    CHECK(reg.size() == 0);
}

void test_registry_single_pair() {
    // The stub registers `fwd_a` / `adj_a` AND `fwd_b` / `adj_b` directly.
    // After the matcher runs, both pairs must be in the registry with
    // the correct adjoint names. This pins the acceptance criterion
    // "map size matches STURM_REGISTER_ADJOINT count; FunctionDecl* key
    // is the forward routine; value is the adjoint's source-level name".
    RegistrySnapshot r = run_registry_matcher("");
    CHECK(r.size == 2);
    CHECK(r.pairs.size() == 2);

    const auto* a = find_forward(r, "fwd_a");
    CHECK(a != nullptr);
    if (a) CHECK_EQ_STR(a->second, std::string("adj_a"));

    const auto* b = find_forward(r, "fwd_b");
    CHECK(b != nullptr);
    if (b) CHECK_EQ_STR(b->second, std::string("adj_b"));
}

void test_registry_extra_pair_in_user_src() {
    // Adding a third pair in the user-supplied source (appended after
    // the stub) must raise the size to 3 with the new pair present.
    constexpr std::string_view extra = R"CPP(
void fwd_c(int, int, int) {}
void adj_c(int, int, int) {}
namespace sturm { namespace _detail {
template <> struct adjoint_of<decltype(&::fwd_c)> {
    static constexpr auto value = &::adj_c;
};
}}
)CPP";
    RegistrySnapshot r = run_registry_matcher(extra);
    CHECK(r.size == 3);
    const auto* c = find_forward(r, "fwd_c");
    CHECK(c != nullptr);
    if (c) CHECK_EQ_STR(c->second, std::string("adj_c"));
}

void test_registry_no_specializations_no_entries() {
    // A TU with forward/adjoint function *definitions* but no
    // specialization of adjoint_of must produce an empty registry —
    // the matcher must NOT fire on bare function declarations.
    constexpr std::string_view code = R"CPP(
namespace sturm { namespace _detail {
template <typename FnPtr> struct adjoint_of;
}}
void loner(int) {}
)CPP";
    RegistrySnapshot r = run_registry_matcher(code, /*prepend_stub=*/false);
    CHECK(r.size == 0);
    CHECK(r.pairs.empty());
}

void test_registry_ignores_non_sturm_adjoint_of() {
    // Negative: a completely different namespace's `adjoint_of` must
    // NOT be picked up by the matcher. The PI-1 matcher's qualified-
    // name guard is load-bearing for the P9 contract — user code
    // cannot accidentally register adjoints for the transpiler.
    constexpr std::string_view code = R"CPP(
namespace other { namespace nested {
template <typename FnPtr> struct adjoint_of;
}}
void fake_fwd(int) {}
void fake_adj(int) {}
namespace other { namespace nested {
template <> struct adjoint_of<decltype(&::fake_fwd)> {
    static constexpr auto value = &::fake_adj;
};
}}
)CPP";
    RegistrySnapshot r = run_registry_matcher(code, /*prepend_stub=*/false);
    CHECK(r.size == 0);
}

void test_registry_key_is_forward_not_adjoint() {
    // Regression guard: the registry's *key* must be the FORWARD
    // FunctionDecl (what the user wrote in `decltype(&::fn)`), not
    // the adjoint. The pairs-by-forward-name lookup above already
    // covers this semantically; here we pin it by running a minimal
    // single-pair TU and verifying exactly one entry whose key name
    // matches the forward.
    constexpr std::string_view code = R"CPP(
namespace sturm { namespace _detail {
template <typename FnPtr> struct adjoint_of;
}}
void only_fwd(int) {}
void only_adj(int) {}
namespace sturm { namespace _detail {
template <> struct adjoint_of<decltype(&::only_fwd)> {
    static constexpr auto value = &::only_adj;
};
}}
)CPP";
    RegistrySnapshot r = run_registry_matcher(code, /*prepend_stub=*/false);
    CHECK(r.size == 1);
    CHECK(r.pairs.size() == 1);
    if (r.pairs.size() == 1) {
        // Key name == forward, value == adjoint. Swapping these would
        // be a PI-1 regression.
        CHECK_EQ_STR(r.pairs.front().first, std::string("only_fwd"));
        CHECK_EQ_STR(r.pairs.front().second, std::string("only_adj"));
    }
}

} // namespace

}  // namespace sturm_test_routine_registry_ns

void run_routine_registry_tests() {
    using namespace sturm_test_routine_registry_ns;
    test_registry_api_basic_contract();
    test_registry_single_pair();
    test_registry_extra_pair_in_user_src();
    test_registry_no_specializations_no_entries();
    test_registry_ignores_non_sturm_adjoint_of();
    test_registry_key_is_forward_not_adjoint();
}
