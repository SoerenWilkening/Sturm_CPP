cmake_minimum_required(VERSION 3.16)

# ── tests/qram/ — runtime entry-point tests for the QRAM-subscript epic ──
#
# sturm-u9ge.13 (Beat D1) — counter-mode stub for `sturm::QRAM_read`
# across the three container shapes from PRD §7. Backend-touching
# targets link the shared `sturm_backend_runtime` OBJECT lib defined in
# tests/CMakeLists.txt (sturm-x4iq) instead of re-listing the 7-file
# backend source set.

add_executable(test_qram_read_stub
    test_qram_read_stub.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/qram/qram_read.cpp
)
target_compile_features(test_qram_read_stub PRIVATE cxx_std_20)
target_link_libraries(test_qram_read_stub PRIVATE sturm_backend_runtime)
add_test(NAME test_qram_read_stub COMMAND test_qram_read_stub)
set_tests_properties(test_qram_read_stub PROPERTIES LABELS "qram;qram-subscript")

# ── sturm-2w6h.1 (Beat B0) — split telemetry counters land on Sink ──
# Pins the new `qrom_read()` / `qreg_read()` virtual hooks on `Sink`,
# the CounterSink overrides that bump split counters, and the
# RecordingSink overrides that record them as ops. The umbrella
# `qram_read()` hook is preserved for ABI continuity (plan
# `docs/plan_qram_backend.md` §5 B0). QROM/qreg dispatch helpers do
# not call the new hooks yet — that lands in B4 (sturm-2w6h.6).
add_executable(test_qram_split_counters test_qram_split_counters.cpp)
target_include_directories(test_qram_split_counters PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qram_split_counters PRIVATE cxx_std_20)
add_test(NAME test_qram_split_counters COMMAND test_qram_split_counters)
set_tests_properties(test_qram_split_counters PROPERTIES LABELS "qram;qram-backend")

# ── sturm-2w6h.2 (Beat B1) — refactor impls to receive (a, n, i, b) ──
# Pins the refactor of `_qram_detail::qram_read_qrom_impl` and
# `qram_read_qreg_impl` from no-arg shims into templates parameterised
# on `(W)`. Bodies remain counter-mode bumps in this beat.
add_executable(test_qram_read_dispatch
    test_qram_read_dispatch.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/qram/qram_read.cpp
)
target_include_directories(test_qram_read_dispatch PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qram_read_dispatch PRIVATE cxx_std_20)
add_test(NAME test_qram_read_dispatch COMMAND test_qram_read_dispatch)
set_tests_properties(test_qram_read_dispatch PROPERTIES LABELS "qram;qram-backend")

# ── sturm-2w6h.6 (Beat B4) — wire split counters through dispatch ──
add_executable(test_qram_telemetry_split
    test_qram_telemetry_split.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/qram/qram_read.cpp
)
target_compile_features(test_qram_telemetry_split PRIVATE cxx_std_20)
target_link_libraries(test_qram_telemetry_split PRIVATE sturm_backend_runtime)
add_test(NAME test_qram_telemetry_split COMMAND test_qram_telemetry_split)
set_tests_properties(test_qram_telemetry_split PROPERTIES LABELS "qram;qram-backend")

# ── sturm-2w6h.7 (Beat B5) — end-to-end gate-stream + statevector test ──
# Closes v1 of the QRAM backend gate-emission epic. Pins the public
# `QRAM_read(a, i, b)` entry-point end-to-end across every layer
# (overload dispatch → mask-OR routing → QROM helper → DSL body →
# adjoint sibling → split telemetry counter).
add_executable(test_qram_read_qrom_gates
    test_qram_read_qrom_gates.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/qram/qram_read.cpp
)
target_compile_features(test_qram_read_qrom_gates PRIVATE cxx_std_20)
target_link_libraries(test_qram_read_qrom_gates PRIVATE sturm_backend_runtime)
add_test(NAME test_qram_read_qrom_gates COMMAND test_qram_read_qrom_gates)
set_tests_properties(test_qram_read_qrom_gates PROPERTIES LABELS "qram;qram-backend")
