// lossy_nested_rewrite.cpp — LO-2e (sturm-rry6) implementation.
#include "lossy_nested_rewrite.hpp"
#include "fresh_names.hpp"
#include "lossy_rewrite_emitter.hpp"
#include "lossy_scope_exit_emitter.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <memory>
#include <sstream>
#include <vector>

using namespace clang;
using namespace clang::ast_matchers;

namespace sturm::transpile {
namespace {

const CompoundStmt* nearest_compound(const Stmt& s, ASTContext& ctx) {
    DynTypedNode cur = DynTypedNode::create(s);
    for (int i = 0; i < 128; ++i) {
        const auto p = ctx.getParents(cur);
        if (p.empty()) return nullptr;
        cur = p[0];
        if (const auto* cs = cur.get<CompoundStmt>()) return cs;
    }
    return nullptr;
}

LossyOpKind kind_of(OverloadedOperatorKind op) {
    switch (op) {
        case OO_StarEqual: case OO_Star:       return LossyOpKind::MulAssign;
        case OO_SlashEqual: case OO_Slash:     return LossyOpKind::DivAssign;
        case OO_PercentEqual: case OO_Percent: return LossyOpKind::ModAssign;
        case OO_AmpEqual: case OO_Amp:         return LossyOpKind::AndAssign;
        case OO_PipeEqual: case OO_Pipe:       return LossyOpKind::OrAssign;
        default:                               return LossyOpKind::MulAssign;
    }
}

class Cb : public MatchFinder::MatchCallback {
public:
    explicit Cb(std::vector<NestedLossyHit>* h) : hits_(h) {}
    void run(const MatchFinder::MatchResult& r) override {
        const auto* o  = r.Nodes.getNodeAs<CXXOperatorCallExpr>("outer");
        const auto* in = r.Nodes.getNodeAs<CXXOperatorCallExpr>("inner");
        const auto* L  = r.Nodes.getNodeAs<DeclRefExpr>("lhs");
        const auto* il = r.Nodes.getNodeAs<DeclRefExpr>("inner_lhs");
        const auto* ir = r.Nodes.getNodeAs<DeclRefExpr>("inner_rhs");
        if (!o || !in || !L || !il || !ir || !r.Context) return;
        const CompoundStmt* b = nearest_compound(*o, *r.Context);
        if (!b) return;
        NestedLossyHit h;
        h.outer_kind = kind_of(o->getOperator());
        h.inner_kind = kind_of(in->getOperator());
        if (auto* d = L->getDecl())  h.lhs_name       = d->getNameAsString();
        if (auto* d = il->getDecl()) h.inner_lhs_name = d->getNameAsString();
        if (auto* d = ir->getDecl()) h.inner_rhs_name = d->getNameAsString();
        h.outer_call = o; h.enclosing_block = b;
        hits_->push_back(std::move(h));
    }
private:
    std::vector<NestedLossyHit>* hits_;
};

auto qint_dre(const char* bind) {
    return declRefExpr(hasType(hasCanonicalType(hasDeclaration(
        cxxRecordDecl(hasName("qint_t")))))).bind(bind);
}

// Outer × inner = 5 × 10 operator-name pairs. We materialise each
// outer pattern once at registration time (see register_*) and let
// MatchFinder anyOf the 10 inner operator names internally.
auto inner_p() {
    return cxxOperatorCallExpr(argumentCountIs(2),
        anyOf(hasOverloadedOperatorName("*"), hasOverloadedOperatorName("/"),
              hasOverloadedOperatorName("%"), hasOverloadedOperatorName("&"),
              hasOverloadedOperatorName("|"), hasOverloadedOperatorName("*="),
              hasOverloadedOperatorName("/="), hasOverloadedOperatorName("%="),
              hasOverloadedOperatorName("&="), hasOverloadedOperatorName("|=")),
        hasArgument(0, ignoringImplicit(qint_dre("inner_lhs"))),
        hasArgument(1, ignoringImplicit(qint_dre("inner_rhs")))).bind("inner");
}

auto outer_p(const char* op) {
    return cxxOperatorCallExpr(hasOverloadedOperatorName(op),
        argumentCountIs(2),
        hasArgument(0, ignoringImplicit(qint_dre("lhs"))),
        hasArgument(1, ignoringImplicit(ignoringParenImpCasts(inner_p())))
    ).bind("outer");
}

// Inner BARE forward — no swap (PRD §2.3 only compound assigns swap).
// Cleanup: strip the leading `swap(...)` line from LO-2c's rendering.
std::string inner_fwd(LossyOpKind k, const std::string& tmp,
                      const std::string& l, const std::string& r) {
    const char* tag = (k == LossyOpKind::MulAssign) ? "mul"
                    : (k == LossyOpKind::AndAssign) ? "and"
                    : (k == LossyOpKind::OrAssign)  ? "or" : "";
    if (!*tag) return {};
    std::ostringstream os;
    os << "qint " << tmp << ";\n" << tag << "_oop("
       << l << ", " << r << ", " << tmp << ");\n";
    return os.str();
}
std::string inner_cu(LossyOpKind k, const std::string& tmp,
                     const std::string& l, const std::string& r) {
    auto cu = emit_lossy_cleanup_text(k, l, r, tmp, "");
    const auto nl = cu.text.find('\n');
    return nl == std::string::npos ? std::string{} : cu.text.substr(nl + 1);
}

} // namespace

void register_nested_lossy_matcher(MatchFinder& f,
                                   std::vector<NestedLossyHit>& hits) {
    static std::vector<std::unique_ptr<Cb>> pool;
    pool.push_back(std::make_unique<Cb>(&hits));
    auto* cb = pool.back().get();
    for (const char* op : {"*=", "/=", "%=", "&=", "|="})
        f.addMatcher(outer_p(op), cb);
}

NestedLossyEmission emit_nested_lossy(const NestedLossyHit& hit,
                                      FreshNameAllocator& alloc) {
    NestedLossyEmission em;
    if (hit.lhs_name.empty() || hit.inner_lhs_name.empty() ||
        hit.inner_rhs_name.empty()) return em;
    LossyEmission inn = emit_lossy_forward_text(
        hit.inner_kind, hit.inner_lhs_name, hit.inner_rhs_name, alloc);
    if (inn.text.empty()) return em;
    em.inner_tmp_name = inn.swap_target_name;
    LossyEmission out = emit_lossy_forward_text(
        hit.outer_kind, hit.lhs_name, em.inner_tmp_name, alloc);
    if (out.text.empty()) return em;
    em.outer_tmp_name = out.swap_target_name;
    em.forward_text = inner_fwd(hit.inner_kind, em.inner_tmp_name,
        hit.inner_lhs_name, hit.inner_rhs_name) + out.text;
    auto cu = emit_lossy_cleanup_text(hit.outer_kind, hit.lhs_name,
        em.inner_tmp_name, em.outer_tmp_name, out.aux_tmp_name);
    em.cleanup_text = cu.text + inner_cu(hit.inner_kind, em.inner_tmp_name,
        hit.inner_lhs_name, hit.inner_rhs_name);
    return em;
}

} // namespace sturm::transpile
