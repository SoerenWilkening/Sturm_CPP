// test_instantiations.cpp — M25: Explicit template instantiation link test.
//
// This TU uses ONLY extern template declarations (suppressing local
// instantiation) and then calls representative methods on each of the four
// canonical widths (4, 8, 16, 32).  If the extern-template / explicit-
// instantiation contract is broken the linker will emit an unresolved-symbol
// error, making this a true link-time test.
//
// Build requirement: compiled WITH STURM_BACKEND_ENABLED; linked against
// the object produced by instantiations.cpp (which carries the definitions).

#include <cstdlib>
#include <cstdio>

// Pull in the full qint interface.  Because STURM_BACKEND_ENABLED is defined
// (set via the CMake target compile definition), qint.hpp emits extern template
// declarations for qint_t<4/8/16/32>.  The explicit definitions are supplied by
// instantiations.cpp which is compiled into the same executable.  This
// combination is the link-time fixture described in M25.
#include "sturm/qtypes/qint.hpp"

// ── minimal test harness ──────────────────────────────────────────────────────
static int g_failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__,    \
                         #cond);                                             \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

// ── helper: exercise key methods on a qint_t<W> value ────────────────────────
// We call constructors, value access, operator+=, operator==, and destructor.
// All of these are defined in the instantiations.cpp object; if the linker
// cannot resolve them the test will fail to link.
template <std::size_t W>
static void exercise() {
    using Q = sturm::qint_t<W>;

    // default constructor + int64_t constructor
    Q a(3);
    Q b(5);

    // operator+= (compound assign declared in qint_core.hpp, body in arith)
    a += Q(2);  // a == 5

    // value member (public)
    CHECK(a.value == 5);

    // operator== returns qbool
    auto eq = (b == a);
    CHECK(eq.value == true);

    // operator< (b < a is false now since both are 5)
    auto lt = (b < a);
    CHECK(lt.value == false);

    // operator[] bit subscript (const overload returns qbool)
    Q c(1);
    auto bit0 = static_cast<const Q&>(c)[0];
    CHECK(bit0.value == true);

    // super_mask starts 0 for classical value
    CHECK(a.super_mask == 0u);
}

int main() {
    exercise<4>();
    exercise<8>();
    exercise<16>();
    exercise<32>();

    if (g_failures == 0) {
        std::printf("OK  test_instantiations: all link-time fixtures passed\n");
        return 0;
    }
    std::fprintf(stderr, "FAIL test_instantiations: %d check(s) failed\n",
                 g_failures);
    return 1;
}
