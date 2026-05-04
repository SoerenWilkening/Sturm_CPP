cmake_minimum_required(VERSION 3.16)

# ── tests/regressions/ — drift-gate regression tests ──────────────────────────
#
# Holds compile-time-only regression tests that pin invariants other beats
# rely on. Each test in this directory is a thin TU whose only signal is
# COMPILES vs DOES-NOT-COMPILE — runtime behaviour is incidental, the
# load-bearing checks are `static_assert`s.
#
# The first inhabitant (sturm-65rs.12 / D1 / sturm-qac.12) pins the D0
# (sturm-65rs.11) re-spelling of internal callsites that need the
# backend `sturm::qint_t<64>` surface (super_mask, qubits[], phi(),
# theta()). If a future refactor flips any of those callsites back to
# bare `sturm::qint` (which post-B1 resolves to the frontend alias),
# the corresponding `static_assert(std::is_same_v<decltype(q),
# sturm::qint_t<64>>)` fires and the regression goes loud-red.

# sturm-65rs.12 (Beat D1 / sturm-qac.12) — drift-gate regression test
# pinning D0's re-spelled callsites to sturm::qint_t<64>. Plan §13 / PRD
# R1 audit.
#
# This target is header-only / pre-transpile in spirit (the TU only
# needs the `sturm/qtypes/qint.hpp` umbrella for `sturm::qint_t<W>` and
# the post-B1 `sturm::qint` alias). No backend sources, no
# STURM_BACKEND_ENABLED — the static_asserts work entirely on the
# header-only frontend type surface.
add_executable(test_qint_callsite_respelling
    test_qint_callsite_respelling.cpp
)
target_include_directories(test_qint_callsite_respelling PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qint_callsite_respelling PRIVATE cxx_std_20)
add_test(NAME test_qint_callsite_respelling
    COMMAND test_qint_callsite_respelling)
set_tests_properties(test_qint_callsite_respelling PROPERTIES
    LABELS "regressions;qram-subscript;qint-alias")
