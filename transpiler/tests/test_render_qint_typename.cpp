// test_render_qint_typename.cpp — sturm-65rs.7 (Beat C0).
//
// Pins the shared `render_qint_typename` helper extracted into
// `transpiler/src/render_qint_typename.hpp`. PRD §4.3 close, plan §8.
//
// Three width spot-checks (8 / 32 / 64) cover the `W > 0` arm — the
// only arm exercised by transpiler-driven type substitution. The
// `W == 0` legacy-`qint` arm stays asserted by the existing emitter
// fixture suites; this file's purpose is to lock in the *one*
// definition site and keep the rendered spelling stable.

#include "render_qint_typename.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using sturm::transpile::render_qint_typename;

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

static void test_render_width_8() {
    CHECK(render_qint_typename(8) == "sturm::qint_t<8>");
}

static void test_render_width_32() {
    CHECK(render_qint_typename(32) == "sturm::qint_t<32>");
}

static void test_render_width_64() {
    CHECK(render_qint_typename(64) == "sturm::qint_t<64>");
}

int main() {
    test_render_width_8();
    test_render_width_32();
    test_render_width_64();

    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
