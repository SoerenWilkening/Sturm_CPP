cmake_minimum_required(VERSION 3.16)

# ── tests/qram/ — runtime entry-point tests for the QRAM-subscript epic ──
#
# sturm-u9ge.13 (Beat D1) — counter-mode stub for `sturm::QRAM_read`
# across the three container shapes from PRD §7. Pinned by
# `tests/qram/test_qram_read_stub.cpp` against the production header
# `include/sturm/qram/qram_read.hpp` and the TU-private body
# `src/sturm/qram/qram_read.cpp`. Counter-mode body bumps a single
# `qram_read` thread-local counter; no gate emission, no backend
# context. Kept off the M19 backend source set deliberately.

add_executable(test_qram_read_stub
    test_qram_read_stub.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/qram/qram_read.cpp
)
target_include_directories(test_qram_read_stub PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qram_read_stub PRIVATE cxx_std_20)
add_test(NAME test_qram_read_stub COMMAND test_qram_read_stub)
set_tests_properties(test_qram_read_stub PROPERTIES LABELS "qram;qram-subscript")

# ── sturm-2w6h.1 (Beat B0) — split telemetry counters land on Sink ──
# Pins the new `qrom_read()` / `qreg_read()` virtual hooks on `Sink`,
# the CounterSink overrides that bump split counters, and the
# RecordingSink overrides that record them as ops. The umbrella
# `qram_read()` hook is preserved for ABI continuity (plan
# `docs/plan_qram_backend.md` §5 B0). QROM/qreg dispatch helpers do
# not call the new hooks yet — that lands in B4 (sturm-2w6h.6).
add_executable(test_qram_split_counters
    test_qram_split_counters.cpp
)
target_include_directories(test_qram_split_counters PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qram_split_counters PRIVATE cxx_std_20)
add_test(NAME test_qram_split_counters COMMAND test_qram_split_counters)
set_tests_properties(test_qram_split_counters PROPERTIES LABELS "qram;qram-backend")
