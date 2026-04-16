// test_matcher_harness.cpp — definitions of the shared test helpers.
//
// See test_matcher_harness.hpp for the role of each exported symbol. The
// stubs below mirror the structure the matcher inspects without pulling
// in the real qtypes subtree.

#include "test_matcher_harness.hpp"

#include "sturm/transpile/matcher.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"

#include <string>
#include <vector>

using namespace sturm::transpile;

int tests_run  = 0;
int tests_pass = 0;

// Minimal stub for the MVP OR matcher: a `qbool` class, a `qint` class
// (used by the wrong-result-type negative), an `operator|` overload, an
// `operator&` overload (wrong-operator negative), `operator|` on qint
// (wrong-result-type negative), and a `foo(qbool, qbool)` call (not an
// operator| negative). The matcher keys off class name alone, so a
// one-line stub suffices.
const std::string_view kQBoolStub = R"CPP(
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
};

class qint {
public:
    qint() {}
    qint(const qbool&) {}
};

inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
inline qbool operator&(const qbool&, const qbool&) { return qbool{}; }
inline qint  operator|(const qint&, const qint&)   { return qint{}; }
inline qbool foo(const qbool&, const qbool&)       { return qbool{}; }

} // namespace sturm

using sturm::qbool;
using sturm::qint;
using sturm::foo;
)CPP";

// Stub for Phase F / Phase G / PH-1 braced-WHEN regression tests. The
// PF-2 matcher anchors on the middle `if` in the three-`if` tower the
// real `WHEN(expr)` macro expands to (include/sturm/control/when.hpp:293).
// We replicate just the structure the matcher inspects: a
// `sturm::detail::materialize_when` overload set, and a three-`if`
// WHEN macro whose middle `if` init-stmt declares `_when_val_` from
// `::sturm::detail::materialize_when(expr)`. WhenCapture / WhenGuard
// are never inspected — a no-op placeholder suffices to make the macro
// compile.
const std::string_view kQBoolWhenStub = R"CPP(
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    bool should_run() const { return true; }
};

inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
inline qbool operator&(const qbool&, const qbool&) { return qbool{}; }

namespace detail {

inline qbool& materialize_when(qbool& q) { return q; }
inline qbool  materialize_when(qbool&& q) { return static_cast<qbool&&>(q); }

struct WhenCapture { WhenCapture() = default; };
inline qbool& make_when_guard(qbool& q) { return q; }

} // namespace detail
} // namespace sturm

using sturm::qbool;

#define WHEN(expr) \
    if (::sturm::detail::WhenCapture _when_capture_{}; true) \
    if (decltype(auto) _when_val_ = ::sturm::detail::materialize_when(expr); true) \
    if (auto& _when_guard_ = ::sturm::detail::make_when_guard(_when_val_); \
        _when_guard_.should_run())
)CPP";

// Run the MVP OR matcher on `user_src` after prepending the qbool stub.
// Returns the populated QUnit. On tool failure the returned unit is left
// empty and the caller reports a CHECK failure.
QUnit run_or_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kQBoolStub.size() + user_src.size());
    code.append(kQBoolStub);
    code.append(user_src);

    QUnit unit;
    clang::ast_matchers::MatchFinder finder;
    register_or_matcher(finder, unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false; tool could not parse "
                     "source\n");
    }
    return unit;
}
