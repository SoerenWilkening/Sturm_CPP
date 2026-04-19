# check_example_or_circuit.cmake — LP5 example-observability ctest driver.
#
# Invoked by tests/transpiler/CMakeLists.txt with:
#   -DSOURCE=<abs path>    — the source example examples/or_circuit.cpp
#   -DGENERATED=<abs path> — the transpiler-generated sibling under
#                            <CMAKE_BINARY_DIR>/sturm_gen/examples/or_circuit.cpp
#
# Contract (PRD acceptance #3):
#   After a clean build that routes examples/or_circuit.cpp through
#   sturm-transpile, the generated file must contain exactly one
#   `uncompute_or(c, a, b);` call injected by the matcher/emitter
#   pipeline, AND the source file must be untouched (the transpiler never
#   rewrites its input in place — it only writes into sturm_gen/).
#
# This test binds the PRD's headline acceptance to a regression check:
# the hermetic snapshot (snapshot_or_single_backend) proves the matcher
# peels the conversion-wrapped `|`-producer chain on a mock (the
# fixture still supplies an expression-template wrapper with a user-
# defined `operator qbool()`; the qbool-level wrappers were retired in
# Phase K PK-2 but the matcher widening remains useful), and this test
# proves the same matcher fires on the real example under the real
# build pipeline.
#
# Assertions:
#   1. GENERATED exists on disk.
#   2. GENERATED contains `uncompute_or(c, a, b)` exactly once — the
#      injected call. The regex tolerates internal whitespace so an
#      emitter tweak that changes formatting does not trip this check.
#   3. SOURCE does NOT contain the exact injected signature
#      `uncompute_or(c, a, b)` — the transpiler must not contaminate its
#      input (no in-place edit, no stray copy). The check mirrors the
#      generated-file regex (same whitespace tolerance) rather than a
#      bare `uncompute_or` substring so the example is free to discuss
#      the helper by name in its documentation comments.
#
# The script exits 0 on success, 1 on any failure.

if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "check_example_or_circuit: SOURCE not set")
endif()
if(NOT DEFINED GENERATED)
    message(FATAL_ERROR "check_example_or_circuit: GENERATED not set")
endif()

if(NOT EXISTS "${GENERATED}")
    message(FATAL_ERROR
        "check_example_or_circuit: generated file missing: ${GENERATED}\n"
        "This file is produced by add_quantum_executable(example_or_circuit).\n"
        "Ensure the example_or_circuit target was built before this test\n"
        "runs (the FIXTURES_SETUP `build_example_or_circuit` handles that).")
endif()
if(NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR
        "check_example_or_circuit: source file missing: ${SOURCE}")
endif()

# ── Assertion 1: GENERATED contains uncompute_or(c, a, b) exactly once ──────
file(READ "${GENERATED}" gen_content)
string(REGEX MATCHALL
    "uncompute_or[ \t]*\\([ \t]*c[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)"
    gen_hits "${gen_content}")
list(LENGTH gen_hits gen_hit_count)
if(NOT gen_hit_count EQUAL 1)
    message(FATAL_ERROR
        "check_example_or_circuit: expected exactly ONE `uncompute_or(c, a, b)` "
        "in generated file, found ${gen_hit_count}.\n"
        "  file: ${GENERATED}\n"
        "PRD acceptance #3: the transpiler must inject uncompute_or(c, a, b) "
        "into the sturm_gen/ sibling of examples/or_circuit.cpp. A count of 0 "
        "means the matcher did not fire on the real example (likely LP4 risk "
        "R1: the mock fixture's AST diverges from the real header's AST). A "
        "count >1 means the emitter fired multiple times on one VarDecl or the "
        "source has a stray reference that leaked through.")
endif()

# ── Assertion 2: SOURCE does not contain the injected signature ─────────────
file(READ "${SOURCE}" src_content)
string(REGEX MATCH
    "uncompute_or[ \t]*\\([ \t]*c[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)"
    src_hit "${src_content}")
if(src_hit)
    message(FATAL_ERROR
        "check_example_or_circuit: the injected signature "
        "`uncompute_or(c, a, b)` appeared in SOURCE ${SOURCE}.\n"
        "The transpiler must never rewrite its input in place. If this file "
        "genuinely needs the call (e.g. a hand-written reference), it belongs "
        "in tests/transpiler/fixtures/, not in examples/.")
endif()

message(STATUS
    "check_example_or_circuit: OK — generated file has 1 `uncompute_or(c, a, b)` "
    "and source is clean")
