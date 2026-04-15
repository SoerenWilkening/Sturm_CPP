// test_fresh_names.cpp — unit tests for transpiler/src/fresh_names.hpp (PE-3).
//
// FreshNameAllocator is the tiny helper that hands out the temporary names
// `__stu_t0`, `__stu_t1`, `__stu_t2`, ... used by the Phase E compound
// matcher. Scope: one allocator per QUnit (i.e. per translation unit), so a
// fresh instance must start from 0 independently of any other instance.
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

int main() {
    test_first_three_allocations();
    test_second_instance_starts_fresh();
    test_return_type_is_std_string();
    test_monotonic_beyond_single_digit();

    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
