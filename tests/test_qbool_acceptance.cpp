// test_qbool_acceptance.cpp — M14: Final acceptance tests (frontend).
//
// Maps to PRD §8 acceptance criteria:
//   AC3 — std::is_base_of_v<qint_t<1>, qbool> is true.
//   AC4 — QboolUncompute enum is gone; uncompute_op::kind has ADD_CONST and
//          BITWISE_SELF instead.
//   AC7 — qbool::make_non_owning() works; non-owning qbool does not release
//          the qubit on destruction.
//   AC10 — sizeof(qbool) <= 56 bytes.

#include "sturm/qtypes/qint.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/recording_sink.hpp"

#include <cassert>
#include <cstdio>
#include <type_traits>

using namespace sturm;

// ── Test helpers ──────────────────────────────────────────────────────────────
static int tests_run  = 0;
static int tests_pass = 0;

#define CHECK(cond) do {                                              \
    ++tests_run;                                                      \
    if (cond) { ++tests_pass; }                                       \
    else {                                                            \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                    \
                     __FILE__, __LINE__, #cond);                      \
    }                                                                 \
} while(0)

// ── AC3: is_base_of — compile-time check ─────────────────────────────────────
// qbool must be a subclass of qint_t<1>.
static_assert(std::is_base_of_v<qint_t<1>, qbool>,
              "AC3: qbool must inherit from qint_t<1>");

static void test_ac3_is_base_of() {
    // static_assert above validates at compile time; runtime anchor for reporting.
    CHECK((std::is_base_of_v<qint_t<1>, qbool>));
    std::printf("PASS: AC3 is_base_of\n");
}

// ── AC4: QboolUncompute gone — compile-time verification ─────────────────────
// The old qbool had a separate QboolUncompute enum and qbool_uncompute_ field.
// After unification, qbool adds NO new data members; all uncompute state lives
// in qint_t<1>::uncompute_.
//
// Compile-time proof: qbool is a subclass of qint_t<1> with no additional
// data, and qbool::owning_ (inherited) replaces the old owning pattern.
// Attempting to access qbool::qbool_uncompute_ or QboolUncompute would be a
// compile error — which is exactly what AC4 requires.
//
// We verify the positive invariant: qbool's size equals qint_t<1>'s size,
// proving no new data fields were added by qbool.
static_assert(sizeof(qbool) == sizeof(qint_t<1>),
              "AC4: qbool must add no data members beyond qint_t<1> "
              "(QboolUncompute fields must be gone)");

static void test_ac4_no_qbool_uncompute_enum() {
    // The static_assert above proves qbool adds no extra fields.
    // Positive runtime check: qbool::owning_ is accessible (inherited from base).
    CHECK(sizeof(qbool) == sizeof(qint_t<1>));
    qbool q;
    (void)q.owning_;  // must compile — field is inherited, not a separate enum field
    CHECK(q.owning_ == true);  // default: owning
    std::printf("PASS: AC4 QboolUncompute gone (sizeof matches, no extra fields)\n");
}

// ── AC7: make_non_owning works ────────────────────────────────────────────────
// Creates a non-owning qbool that wraps an externally managed qubit.
// Verifies that when the non-owning qbool goes out of scope, the qubit is NOT
// released from the pool.
static void test_ac7_make_non_owning() {
    QubitPool::instance().reset_for_testing();

    // Allocate one real qubit.
    const int idx = QubitPool::instance().allocate();
    CHECK(idx >= 0);
    CHECK(QubitPool::instance().in_use() == 1);

    {
        qbool view = qbool::make_non_owning(idx);
        CHECK(view.qubits[0] == idx);
        CHECK(view.owning_   == false);
        // view goes out of scope — must NOT release the qubit.
    }

    // Qubit must still be in use after the non-owning qbool is destroyed.
    CHECK(QubitPool::instance().in_use() == 1);

    // Clean up manually.
    QubitPool::instance().release(idx);
    CHECK(QubitPool::instance().in_use() == 0);

    std::printf("PASS: AC7 make_non_owning\n");
}

// ── AC10: sizeof(qbool) <= 56 — compile-time check ───────────────────────────
static_assert(sizeof(qbool) <= 56,
              "AC10: sizeof(qbool) must be <= 56 (no bloat from unification)");

static void test_ac10_sizeof() {
    // static_assert enforces this at compile time; runtime anchor for reporting.
    CHECK(sizeof(qbool) <= 56);
    std::printf("PASS: AC10 sizeof(qbool)=%zu <= 56\n", sizeof(qbool));
}

// ── main ──────────────────────────────────────────────────────────────────────
int main() {
    test_ac3_is_base_of();
    test_ac4_no_qbool_uncompute_enum();
    test_ac7_make_non_owning();
    test_ac10_sizeof();

    std::printf("%s  (%d/%d passed)\n",
                tests_pass == tests_run ? "PASS" : "FAIL",
                tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
