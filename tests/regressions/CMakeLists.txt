cmake_minimum_required(VERSION 3.16)

# ── tests/regressions/ — drift-gate regression tests ──────────────────────────
#
# Holds compile-time-only regression tests that pin invariants other beats
# rely on. Each test in this directory is a thin TU whose only signal is
# COMPILES vs DOES-NOT-COMPILE — runtime behaviour is incidental, the
# load-bearing checks are `static_assert`s.
#
# The first inhabitant (sturm-65rs.12 / D1 / sturm-qac.12) pinned the D0
# (sturm-65rs.11) re-spelling of internal callsites that need the backend
# `sturm::qint_t<64>` surface (super_mask, qubits[], phi(), theta()). If
# a future refactor flips any of those callsites back to bare
# `sturm::qint` (which post-B1 resolves to the frontend alias), the
# corresponding `static_assert(std::is_same_v<decltype(q),
# sturm::qint_t<64>>)` fires and the regression goes loud-red.
#
# sturm-1os7 (D1 split): the original single-file gate hit the 200-LoC
# budget cap (plan §1 / PRD §R4 budget targets). Rather than renegotiate
# a higher budget, the gate is split per backend-surface family — each
# file holds anchors for one surface (super_mask/qubits[], phi()/theta(),
# compound-assigns, .value, lifecycle) plus a single shared cross-checks
# file. New D0-miss extensions add anchors to the matching per-surface
# file without renegotiating budget. Per the issue's "scales linearly
# without budget renegotiation" guidance.

# Common helper: register a regression-test executable that needs only
# the qtypes/qint.hpp umbrella (header-only, no backend sources, no
# STURM_BACKEND_ENABLED — the static_asserts work entirely on the
# header-only frontend type surface).
function(_sturm_add_drift_gate name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(${name} PRIVATE cxx_std_20)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        LABELS "regressions;qram-subscript;qint-alias")
endfunction()

# Per-surface drift-gate files (sturm-1os7 split). Each file pins anchors
# for one backend-surface family. Adding a new D0-miss anchor: drop it
# into the matching per-surface file and rebuild that target.
_sturm_add_drift_gate(test_qint_callsite_respelling_super_mask
    test_qint_callsite_respelling_super_mask.cpp)
_sturm_add_drift_gate(test_qint_callsite_respelling_phi_theta
    test_qint_callsite_respelling_phi_theta.cpp)
_sturm_add_drift_gate(test_qint_callsite_respelling_compound_assign
    test_qint_callsite_respelling_compound_assign.cpp)
_sturm_add_drift_gate(test_qint_callsite_respelling_value
    test_qint_callsite_respelling_value.cpp)
_sturm_add_drift_gate(test_qint_callsite_respelling_lifecycle
    test_qint_callsite_respelling_lifecycle.cpp)

# Shared cross-checks: sturm::frontend::qint != sturm::qint_t<64> AND
# sturm::qint resolves to sturm::frontend::qint. Both invariants are
# load-bearing — if either fails, every per-surface anchor degenerates
# into a tautology.
_sturm_add_drift_gate(test_qint_callsite_respelling_xchecks
    test_qint_callsite_respelling_xchecks.cpp)

# ── sturm-zbzo (Frontend simpl. P2.a / G5 / PRD A4) ──────────────────────
# Tree-grep audit gate against re-introduction of any qubit-cap
# artefact. After P2.a, the production trees (include/, src/,
# transpiler/src/) must contain none of the deleted compile-time knob,
# the deleted abort sentinel constant, or the deleted abort-message
# body fragment.  See test_no_cap_artifacts.cpp for the forbidden
# token list.
#
# Test files / docs / examples may legitimately mention these for
# historical or migration prose — the scan is restricted to production
# code. The test is a header-only TU that walks the configured source
# root at run time using <filesystem> + std::ifstream.
add_executable(test_no_cap_artifacts
    test_no_cap_artifacts.cpp
)
target_compile_features(test_no_cap_artifacts PRIVATE cxx_std_20)
target_compile_definitions(test_no_cap_artifacts PRIVATE
    "STURM_SOURCE_ROOT=\"${CMAKE_SOURCE_DIR}\"")
add_test(NAME test_no_cap_artifacts COMMAND test_no_cap_artifacts)
set_tests_properties(test_no_cap_artifacts PROPERTIES
    LABELS "regressions;frontend-simplification;qubit-cap-removal")

# ── sturm-yggr (Frontend simpl. P8 / PRD A1) ─────────────────────────────
# Byte-identical regression gate: after Phase 8 migrates
# `examples/qram_demo.cpp` to the new umbrella + opt-in-header shape
# (PRD §4 "After"), the rewritten demo's stdout must match the
# pre-rewrite baseline `tests/regressions/data/qram_demo_baseline.txt`
# (R4 mitigation: baseline captured at the START of Phase 8 so
# incidental ordering shifts from Phases 1–7 are reflected in it).
#
# The test runs the built `example_qram_demo` via popen, captures its
# stdout, and compares against the committed baseline file byte for
# byte. On mismatch, the first 50 lines of each side are emitted to
# stderr for triage.
add_executable(test_qram_demo_byte_identical
    test_qram_demo_byte_identical.cpp
)
target_compile_features(test_qram_demo_byte_identical PRIVATE cxx_std_20)
target_compile_definitions(test_qram_demo_byte_identical PRIVATE
    "EXAMPLE_BINARY_PATH=\"$<TARGET_FILE:example_qram_demo>\""
    "EXPECTED_OUTPUT_PATH=\"${CMAKE_SOURCE_DIR}/tests/regressions/data/qram_demo_baseline.txt\"")
add_dependencies(test_qram_demo_byte_identical example_qram_demo)
add_test(NAME test_qram_demo_byte_identical
         COMMAND test_qram_demo_byte_identical)
set_tests_properties(test_qram_demo_byte_identical PROPERTIES
    LABELS "regressions;frontend-simplification;qram;byte-identical")
