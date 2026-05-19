// test_qram_e2e.cpp -- sturm-u9ge.17 (Beat G1) end-to-end gate.
//
// Plan §10 / G1; PRD §5, §8, §11.1, §11.2, §11.4. Closes the QRAM-via-
// array-subscript epic by exercising matcher (C1) + emitter (D2) +
// runtime stub (D1) across all three PRD §7 container shapes (StdArray /
// CArray / Pointer) under both counter-mode and circuit-mode sinks.
// Assertions per shape: (1) rewritten buffer contains no
// `.operator size_t(` / `.operator unsigned long(` substrings (PRD §5);
// (2) `sturm::QRAM_read` invoked exactly once per source subscript
// (per-sink counter); (3) `i.super_mask` unchanged across the call
// (§11.2.1); (4) adjoint round-trip increments the counter again, mask
// still unchanged (D0d). LoC budget: <= 300. STURM_BACKEND_ENABLED
// flag mirrors test_qram_read_stub.cpp (qram_read.hpp's macro gate).
#define STURM_BACKEND_ENABLED 1

#include "qram_emitter.hpp"
#include "matcher_qram_subscript.hpp"

#include "sturm/qram/qram_read.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/routines/invert.hpp"
#include "sturm/core/counter_sink.hpp"
#include "sturm/core/recording_sink.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/StringRef.h"

#include <array>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using sturm::transpile::emit_qram_rewrites;
using sturm::transpile::QramSubscriptHit;
using sturm::transpile::register_qram_subscript_matcher;

static int tests_run = 0, tests_pass = 0;
#define CHECK(cond) do { ++tests_run;                                 \
    if (cond) { ++tests_pass; }                                       \
    else { std::fprintf(stderr, "FAIL  %s:%d  %s\n",                  \
                        __FILE__, __LINE__, #cond); } } while (0)

namespace {

// Hermetic stub: backend `qint_t<W>` (super_mask field), frontend
// `qint` carrying the load-bearing implicit `operator unsigned long()`
// (UDC discriminator the C1 matcher pivots on per PRD §7), minimal
// `array<T, N>` stand-in for the StdArray arm.
constexpr std::string_view kStub = R"CPP(
namespace sturm {
template <int W> class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
};
namespace frontend {
class qint {
public:
    qint() noexcept = default;
    qint(long long v) noexcept : value_(v) {}
    qint(const qint&) noexcept = default;
    template <int W> qint(const qint_t<W>&) noexcept {}
    operator unsigned long() const noexcept {
        return static_cast<unsigned long>(value_);
    }
private:
    long long value_ = 0;
};
} // namespace frontend
} // namespace sturm
using qint = sturm::frontend::qint;
template <typename T, unsigned long N>
struct array {
    T data_[N];
    T& operator[](unsigned long i)             { return data_[i]; }
    const T& operator[](unsigned long i) const { return data_[i]; }
};
)CPP";

// One body per PRD §7 row — each enclosing function is annotated
// reversible so the D2 emitter plants the matching adjoint per D0d.5.
constexpr std::string_view kBodyStdArray = R"CPP(
[[clang::annotate("sturm::reversible")]]
void demo(qint i) {
    array<sturm::qint_t<8>, 4> a;
    qint b = a[i]; (void)b;
})CPP";
constexpr std::string_view kBodyCArray = R"CPP(
[[clang::annotate("sturm::reversible")]]
void demo(qint i) {
    sturm::qint_t<16> a[4];
    qint b = a[i]; (void)b;
})CPP";
constexpr std::string_view kBodyPointer = R"CPP(
[[clang::annotate("sturm::reversible")]]
void demo(sturm::qint_t<32>* a, unsigned long n, qint i) {
    qint b = a[i]; (void)b; (void)n;
})CPP";

// ── Tooling glue (mirrors test_qram_emitter.cpp) ────────────────────────────
using ProbeFn = std::function<void(clang::ASTContext&, clang::Rewriter&)>;
struct E2eConsumer : public clang::ASTConsumer {
    E2eConsumer(ProbeFn p, clang::Rewriter* r) : p_(std::move(p)), r_(r) {}
    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        r_->setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
        if (p_) p_(ctx, *r_);
    }
    ProbeFn p_; clang::Rewriter* r_;
};
class E2eAction : public clang::ASTFrontendAction {
public:
    E2eAction(ProbeFn p, clang::Rewriter* r, std::string* o)
        : probe_(std::move(p)), rw_(r), out_(o) {}
    std::unique_ptr<clang::ASTConsumer>
    CreateASTConsumer(clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<E2eConsumer>(probe_, rw_);
    }
    void EndSourceFileAction() override {
        const clang::SourceManager& sm = rw_->getSourceMgr();
        const clang::FileID main = sm.getMainFileID();
        const clang::RewriteBuffer* buf = rw_->getRewriteBufferFor(main);
        if (buf != nullptr) {
            llvm::raw_string_ostream os(*out_); buf->write(os); os.flush();
        } else {
            llvm::StringRef c = sm.getBufferData(main);
            out_->assign(c.data(), c.size());
        }
    }
private:
    ProbeFn probe_; clang::Rewriter* rw_; std::string* out_;
};
class E2eFactory : public clang::tooling::FrontendActionFactory {
public:
    E2eFactory(ProbeFn p, clang::Rewriter* r, std::string* o)
        : probe_(std::move(p)), rw_(r), out_(o) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<E2eAction>(probe_, rw_, out_);
    }
private:
    ProbeFn probe_; clang::Rewriter* rw_; std::string* out_;
};

// Run C1 + D2 against (stub + body), return the rewritten main-file.
std::string transpile(std::string_view body) {
    std::string src;
    src.reserve(kStub.size() + body.size());
    src.append(kStub);
    src.append(body);
    clang::Rewriter rw;
    std::string out;
    auto probe = [](clang::ASTContext& ctx, clang::Rewriter& r) {
        clang::ast_matchers::MatchFinder finder;
        std::vector<QramSubscriptHit> hits;
        register_qram_subscript_matcher(finder, hits);
        finder.matchAST(ctx);
        emit_qram_rewrites(r, hits);
    };
    E2eFactory factory(std::move(probe), &rw, &out);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), src, args, "qram_e2e_input.cpp");
    if (!ok) std::fprintf(stderr, "FAIL  tool run on qram_e2e_input.cpp\n");
    return out;
}

// Circuit-mode sink: RecordingSink (codebase's circuit-mode sink per
// B1a) does not override `qram_read`; subclass and push a Record so
// the test counts gate-stream invocations the same way it counts
// CounterSink bumps.
class CircuitSink : public sturm::RecordingSink {
public:
    void qram_read() override {
        sturm::Record r; r.op = "qram_read"; r.control = -1;
        records_.push_back(std::move(r));
    }
    std::size_t qram_read_count() const {
        std::size_t n = 0;
        for (const auto& r : records_) if (r.op == "qram_read") ++n;
        return n;
    }
private:
    std::vector<sturm::Record> records_;
};

std::size_t count_occurrences(std::string_view hay, std::string_view needle) {
    std::size_t n = 0, pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string_view::npos) {
        ++n; pos += needle.size();
    }
    return n;
}

// (1) no call-site UDC tokens survive; (2) exactly one runtime call;
// (3) exactly one adjoint planted at reversible-scope close brace.
void check_pipeline(std::string_view label, std::string_view body) {
    std::fprintf(stderr, "  pipeline: %.*s\n",
                 static_cast<int>(label.size()), label.data());
    const std::string out = transpile(body);
    CHECK(!out.empty());
    CHECK(out.find(".operator size_t(") == std::string::npos);
    CHECK(out.find(".operator unsigned long(") == std::string::npos);
    CHECK(count_occurrences(out, "::sturm::QRAM_read(") == 1u);
    CHECK(count_occurrences(out, "::sturm::invert<&::sturm::QRAM_read>()") == 1u);
}

// ── Runtime sink-mode assertions (counter + circuit) ────────────────────────
template <std::size_t W>
sturm::qint_t<W> qcl(std::int64_t v) noexcept { return sturm::qint_t<W>(v); }

// Each shape: install the sink, populate a classical container, fire
// forward + adjoint, assert (counter delta = 1 per call, mask
// unchanged). Body parameterised on the counter lambda so the same
// code runs under CounterSink and CircuitSink.
// W=8 across all three shapes because qram_read.hpp only registers
// `STURM_REGISTER_ADJOINT(QRAM_read<8u>, __QRAM_read_adj<8u>)` (the
// other widths are deferred per the header's TODO note); using a
// single width keeps the pointer-arm `invert<&fn>()` lookup well-
// formed alongside the array shapes' direct-call adjoints.
template <class Sink, class CountFn>
void run_std_array_shape(Sink& s, CountFn count) {
    constexpr std::size_t W = 8, N = 4;
    sturm::ScopedSink scope(&s);
    std::array<sturm::qint_t<W>, N> a{};
    for (std::size_t k = 0; k < N; ++k) a[k] = qcl<W>(k);
    sturm::qint_t<W> i = qcl<W>(2), b = qcl<W>(0);
    const auto m0 = i.super_mask;
    const std::size_t c0 = count(s);
    sturm::QRAM_read(a, i, b);
    CHECK(count(s) == c0 + 1u); CHECK(i.super_mask == m0);
    sturm::__QRAM_read_adj(a, i, b);
    CHECK(count(s) == c0 + 2u); CHECK(i.super_mask == m0);
}
template <class Sink, class CountFn>
void run_c_array_shape(Sink& s, CountFn count) {
    constexpr std::size_t W = 8, N = 4;
    sturm::ScopedSink scope(&s);
    sturm::qint_t<W> a[N]{};
    for (std::size_t k = 0; k < N; ++k) a[k] = qcl<W>(k * 2);
    sturm::qint_t<W> i = qcl<W>(1), b = qcl<W>(0);
    const auto m0 = i.super_mask;
    const std::size_t c0 = count(s);
    sturm::QRAM_read(a, i, b);
    CHECK(count(s) == c0 + 1u); CHECK(i.super_mask == m0);
    sturm::__QRAM_read_adj(a, i, b);
    CHECK(count(s) == c0 + 2u); CHECK(i.super_mask == m0);
}
template <class Sink, class CountFn>
void run_pointer_shape(Sink& s, CountFn count) {
    constexpr std::size_t W = 8, N = 4;
    sturm::ScopedSink scope(&s);
    sturm::qint_t<W> storage[N]{};
    for (std::size_t k = 0; k < N; ++k) storage[k] = qcl<W>(k + 7);
    const sturm::qint_t<W>* a = storage;
    sturm::qint_t<W> i = qcl<W>(3), b = qcl<W>(0);
    const auto m0 = i.super_mask;
    const std::size_t c0 = count(s);
    sturm::QRAM_read(a, N, i, b);
    CHECK(count(s) == c0 + 1u); CHECK(i.super_mask == m0);
    // P9 round-trip via `invert<&fn>()` — D0d.4 audit-friendly token.
    constexpr auto adj_ptr = sturm::invert<&sturm::QRAM_read<W>>();
    static_assert(adj_ptr == &sturm::__QRAM_read_adj<W>,
                  "invert<&QRAM_read<W>>() must resolve to "
                  "&__QRAM_read_adj<W> per D0d.");
    adj_ptr(a, N, i, b);
    CHECK(count(s) == c0 + 2u); CHECK(i.super_mask == m0);
}
void exercise_counter() {
    auto count = [](sturm::CounterSink& s) { return s.count("qram_read"); };
    sturm::CounterSink s1; run_std_array_shape(s1, count);
    sturm::CounterSink s2; run_c_array_shape (s2, count);
    sturm::CounterSink s3; run_pointer_shape (s3, count);
}
void exercise_circuit() {
    auto count = [](CircuitSink& s) { return s.qram_read_count(); };
    CircuitSink s1; run_std_array_shape(s1, count);
    CircuitSink s2; run_c_array_shape (s2, count);
    CircuitSink s3; run_pointer_shape (s3, count);
}

}  // namespace

int run_test_qram_e2e(int /*argc*/, char** /*argv*/) {
    check_pipeline("StdArray", kBodyStdArray);
    check_pipeline("CArray",   kBodyCArray);
    check_pipeline("Pointer",  kBodyPointer);
    exercise_counter();
    exercise_circuit();
    std::fprintf(stderr, "test_qram_e2e: %d / %d checks passed\n",
                 tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
