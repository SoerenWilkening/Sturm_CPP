// test_fresh_names.cpp — unit tests for transpiler/src/fresh_names.hpp
// (PE-3, extended for PG-0).
//
// FreshNameAllocator is the tiny helper that hands out the temporary names
// `__stu_t0`, `__stu_t1`, `__stu_t2`, ... used by the Phase E compound
// matcher. Scope: one allocator per QUnit (i.e. per translation unit), so a
// fresh instance must start from 0 independently of any other instance.
//
// Phase G adds a second, *independent* counter behind `next_ctrl()` that hands
// out `__stu_ctrl0`, `__stu_ctrl1`, ... for the nested-WHEN AND temporaries.
// The two counters must never cross-contaminate: interleaved calls to `next()`
// and `next_ctrl()` have to produce the expected `__stu_t` / `__stu_ctrl`
// sequences with no numbering skips on either side.
//
// Covers:
//   - First three allocations on a fresh instance return __stu_t0, __stu_t1,
//     __stu_t2 in order.
//   - A *second* freshly-constructed instance also starts from 0 (proving the
//     counter is per-instance, not global).
//   - The returned value is a std::string whose exact contents match the
//     `__stu_t<N>` spelling from docs/implementation_plan_transpiler_phase_e.md
//     (no underscore between `stu` and `t`; N is a decimal integer with no
//     leading zeros).
//   - A larger run (10 allocations) still produces the expected sequence, so
//     the monotonic counter survives beyond single digits.
//   - [PG-0] First three `next_ctrl()` calls return __stu_ctrl0/1/2 on a
//     fresh instance.
//   - [PG-0] A second instance's `next_ctrl()` counter also starts at 0.
//   - [PG-0] `next_ctrl()` is monotonic past single digits, same as `next()`.
//   - [PG-0] Interleaving `next()` and `next_ctrl()` keeps the two counters
//     fully independent: both sequences come out in order with no skips,
//     regardless of call order.

#include "fresh_names.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using sturm::transpile::FreshNameAllocator;

// ── Test harness ──────────────────────────────────────────────────────────────
static int tests_run  = 0;
static int tests_pass = 0;

#define CHECK(cond) do {                                              \
    ++tests_run;                                                      \
    if (cond) { ++tests_pass; }                                       \
    else {                                                            \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                     \
                     __FILE__, __LINE__, #cond);                      \
    }                                                                 \
} while (0)

static void test_first_three_allocations() {
    FreshNameAllocator a;
    const std::string n0 = a.next();
    const std::string n1 = a.next();
    const std::string n2 = a.next();
    CHECK(n0 == "__stu_t0");
    CHECK(n1 == "__stu_t1");
    CHECK(n2 == "__stu_t2");
}

static void test_second_instance_starts_fresh() {
    FreshNameAllocator a;
    (void)a.next();   // __stu_t0
    (void)a.next();   // __stu_t1
    (void)a.next();   // __stu_t2

    // A second, independently-constructed allocator must NOT pick up where
    // `a` left off — the counter is per-instance. This is the guarantee the
    // roadmap assumes when it says "Per-QUnit scope" in
    // docs/implementation_plan_transpiler_phase_e.md.
    FreshNameAllocator b;
    CHECK(b.next() == "__stu_t0");
    CHECK(b.next() == "__stu_t1");
}

static void test_return_type_is_std_string() {
    FreshNameAllocator a;
    // Assigning into a std::string (not string_view / char*) locks in the
    // documented ownership: the caller is free to keep the value past the
    // allocator's lifetime.
    std::string s = a.next();
    CHECK(s == "__stu_t0");
}

static void test_monotonic_beyond_single_digit() {
    FreshNameAllocator a;
    for (int i = 0; i < 10; ++i) {
        std::string expected = "__stu_t" + std::to_string(i);
        std::string got = a.next();
        CHECK(got == expected);
    }
}

// ── PG-0 (Phase G) tests for next_ctrl() ────────────────────────────────────

static void test_next_ctrl_first_three_allocations() {
    // Mirror test_first_three_allocations, but for the independent control
    // counter. Phase G uses these names for the injected AND-temp:
    //   `qbool __stu_ctrl0 = outer & inner;`
    FreshNameAllocator a;
    const std::string n0 = a.next_ctrl();
    const std::string n1 = a.next_ctrl();
    const std::string n2 = a.next_ctrl();
    CHECK(n0 == "__stu_ctrl0");
    CHECK(n1 == "__stu_ctrl1");
    CHECK(n2 == "__stu_ctrl2");
}

static void test_next_ctrl_second_instance_starts_fresh() {
    // Each allocator owns its own pair of counters. A burn-in on one instance
    // must not bleed into another instance's ctrl counter.
    FreshNameAllocator a;
    (void)a.next_ctrl();  // __stu_ctrl0
    (void)a.next_ctrl();  // __stu_ctrl1

    FreshNameAllocator b;
    CHECK(b.next_ctrl() == "__stu_ctrl0");
    CHECK(b.next_ctrl() == "__stu_ctrl1");
}

static void test_next_ctrl_monotonic_beyond_single_digit() {
    // Same beyond-single-digit sanity check as for `next()` — the ctrl counter
    // is just a plain std::size_t, but asserting this locks in the spelling
    // rule (no leading zeros, decimal only).
    FreshNameAllocator a;
    for (int i = 0; i < 10; ++i) {
        std::string expected = "__stu_ctrl" + std::to_string(i);
        std::string got = a.next_ctrl();
        CHECK(got == expected);
    }
}

static void test_next_and_next_ctrl_are_independent() {
    // The core PG-0 acceptance criterion: interleaving `next()` and
    // `next_ctrl()` must produce the correct monotonic sequence on each side
    // with zero cross-contamination. If the counters were shared or if one
    // incremented the other, either the __stu_t sequence or the __stu_ctrl
    // sequence would skip numbers.
    FreshNameAllocator a;

    CHECK(a.next()      == "__stu_t0");       // t: 0
    CHECK(a.next_ctrl() == "__stu_ctrl0");    // ctrl: 0
    CHECK(a.next()      == "__stu_t1");       // t: 1
    CHECK(a.next()      == "__stu_t2");       // t: 2
    CHECK(a.next_ctrl() == "__stu_ctrl1");    // ctrl: 1
    CHECK(a.next_ctrl() == "__stu_ctrl2");    // ctrl: 2
    CHECK(a.next()      == "__stu_t3");       // t: 3

    // Reversed ordering should also preserve independence — starting with
    // ctrl then switching to t.
    FreshNameAllocator b;
    CHECK(b.next_ctrl() == "__stu_ctrl0");
    CHECK(b.next()      == "__stu_t0");
    CHECK(b.next_ctrl() == "__stu_ctrl1");
    CHECK(b.next()      == "__stu_t1");
}

int main() {
    test_first_three_allocations();
    test_second_instance_starts_fresh();
    test_return_type_is_std_string();
    test_monotonic_beyond_single_digit();

    // PG-0 additions
    test_next_ctrl_first_three_allocations();
    test_next_ctrl_second_instance_starts_fresh();
    test_next_ctrl_monotonic_beyond_single_digit();
    test_next_and_next_ctrl_are_independent();

    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
