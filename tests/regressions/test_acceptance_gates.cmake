# test_acceptance_gates.cmake — sturm-f8ib / Frontend simpl. P9 (Plan
# §3 P9 / PRD §8 acceptance criteria roll-up).
#
# Wires the seven PRD acceptance criteria A1–A7 as named ctests. Most
# delegate to existing F-1..F-8 tests; A2 / A3 add fresh-build sub-
# steps; A1 layers a textual diff against the PRD §4 "After" listing
# on top of the byte-identical sub-gate; A7 is a doc grep. ALL seven
# gates must be green in a clean -DSTURM_FULL_TEST_SUITE=ON build.
#
# This file is `include()`d from `tests/regressions/CMakeLists.txt`.
# Per the issue's ≤120 LOC budget, heavier driver logic lives in
# sibling scripts under `tests/regressions/acceptance/`.

set(_acc_dir "${CMAKE_CURRENT_LIST_DIR}/acceptance")

# Helper: register a "delegate" gate that re-runs the named ctest from
# the parent build via `ctest -R '^<name>$'`. Passes iff the delegate
# passes. The double invocation is intentional — the gate's signal
# stays distinct from the delegate so a CI run reports both names.
function(_acc_delegate gate_name delegate_name)
    add_test(NAME ${gate_name}
        COMMAND ctest --test-dir "${CMAKE_BINARY_DIR}"
                      -R "^${delegate_name}$"
                      --output-on-failure
                      --no-tests=error)
    set_tests_properties(${gate_name} PROPERTIES
        LABELS "regressions;frontend-simplification;acceptance-gate")
endfunction()

# ── A1 — qram_demo byte-identical + textual diff vs PRD §4 ─────────────
# Part (a): byte-identical regression — delegate to F-8.
_acc_delegate(acceptance_A1_byte_identical test_qram_demo_byte_identical)
# Part (b): textual diff between examples/qram_demo.cpp and the PRD §4
# "After" listing (sturm-f8ib reconciliation: PRD listing was updated
# to match the demo, option (a) in the issue NOTES).
add_test(NAME acceptance_A1_textual_diff
    COMMAND "${CMAKE_COMMAND}"
            "-DPRD_PATH=${CMAKE_SOURCE_DIR}/docs/prd_frontend_simplification.md"
            "-DDEMO_PATH=${CMAKE_SOURCE_DIR}/examples/qram_demo.cpp"
            -P "${_acc_dir}/run_a1_diff.cmake")
set_tests_properties(acceptance_A1_textual_diff PROPERTIES
    LABELS "regressions;frontend-simplification;acceptance-gate")

# ── A2 / A3 — fresh-dir cmake -S … -B _AX configure + build + ctest ────
# Both spawn ~3-5 minutes of work; gated behind STURM_FULL_TEST_SUITE.
if(STURM_FULL_TEST_SUITE)
    foreach(_pair "A2;APPEND" "A3;SIMULATE")
        list(GET _pair 0 _gate)
        list(GET _pair 1 _mode)
        add_test(NAME acceptance_${_gate}_fresh_build
            COMMAND "${CMAKE_COMMAND}"
                    "-DSTURM_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
                    "-DSCRATCH_ROOT=${CMAKE_BINARY_DIR}/acceptance_${_gate}"
                    "-DGATE_LABEL=${_gate}"
                    "-DSTURM_MODE_VALUE=${_mode}"
                    "-DPARENT_LLVM_DIR=${LLVM_DIR}"
                    "-DPARENT_CLANG_DIR=${Clang_DIR}"
                    "-DPARENT_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
                    "-DPARENT_C_COMPILER=${CMAKE_C_COMPILER}"
                    -P "${_acc_dir}/run_a2_a3_fresh_build.cmake")
        set_tests_properties(acceptance_${_gate}_fresh_build PROPERTIES
            LABELS "regressions;frontend-simplification;acceptance-gate"
            TIMEOUT 1800
            PROCESSORS 6)
    endforeach()
endif()

# ── A4 — no surviving cap-enforcement code (delegate to F-2a) ──────────
_acc_delegate(acceptance_A4_no_cap_artifacts test_no_cap_artifacts)

# ── A5 — umbrella + opt-in qram header (delegate to F-3 + F-4) ─────────
_acc_delegate(acceptance_A5_umbrella_only test_umbrella_only)
_acc_delegate(acceptance_A5_umbrella_qram test_umbrella_qram)

# ── A6 — STURM_NO_AUTO_LIFECYCLE escape hatch (delegate to F-7) ────────
_acc_delegate(acceptance_A6_main_lifecycle_no_auto
              snapshot_main_lifecycle_no_auto)

# ── A7 — docs reflect the new shape (no pre-PRD example tokens) ────────
add_test(NAME acceptance_A7_doc_shape
    COMMAND "${CMAKE_COMMAND}"
            "-DDOC_PATHS=${CMAKE_SOURCE_DIR}/docs/qram_user_intro.md;${CMAKE_SOURCE_DIR}/docs/getting_started.md"
            -P "${_acc_dir}/run_a7_doc_grep.cmake")
set_tests_properties(acceptance_A7_doc_shape PROPERTIES
    LABELS "regressions;frontend-simplification;acceptance-gate")
