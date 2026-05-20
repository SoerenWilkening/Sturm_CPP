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

# ── sturm-44bt.3 (Beat BB3) -- lib_qram_read_bb_dsl gate-stream test.
# REWRITES v1's test_qram_read_qrom_gates.cpp for the BB DSL helper:
# pins the §4.4 closed-form CCX/CNOT/X budget at (N=4, W=4) recomputed
# from the formula (no magic numbers), the gate-set discipline
# (records ⊆ {quantum_xor, quantum_and}), and the i.value / i.super_mask
# invariants. Counter-mode bumps (qrom_read / qreg_read split + umbrella)
# are wired by BB4 at the public QRAM_read site; the BB3 DSL helper
# leaves a TODO(BB4) comment instead of asserting them here.
add_executable(test_qram_read_bb_qrom_gates
    test_qram_read_bb_qrom_gates.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/qram/qram_read.cpp
)
target_compile_features(test_qram_read_bb_qrom_gates PRIVATE cxx_std_20)
target_link_libraries(test_qram_read_bb_qrom_gates PRIVATE sturm_backend_runtime)
add_test(NAME test_qram_read_bb_qrom_gates COMMAND test_qram_read_bb_qrom_gates)
set_tests_properties(test_qram_read_bb_qrom_gates PROPERTIES LABELS "qram;qram-backend")

# ── sturm-44bt.3 (Beat BB3) -- lib_qram_read_bb_dsl round-trip test.
# Pins G3+G6 at the DSL layer. Statevector legs at (N=2, W=4) and
# (N=4, W=2) sweep classical i ∈ [0, N) and assert P(b == a[i]) ≈ 1
# after forward, then P(b == 0) ≈ 1 after adjoint. RecordingSink leg
# at (N=8, W=4) — too large for orkan (kMaxQubits=17) — asserts every
# (op, qubits, controls) triple appears even-times across fwd + adj
# (gate-stream balance pin). Superposed-i leg at (N=2, W=4) verifies
# fwd + adj zeros b on a superposed address and leaves i.super_mask
# unchanged.
add_executable(test_qram_read_bb_round_trip
    test_qram_read_bb_round_trip.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/qram/qram_read.cpp
)
target_compile_features(test_qram_read_bb_round_trip PRIVATE cxx_std_20)
target_link_libraries(test_qram_read_bb_round_trip PRIVATE sturm_backend_runtime)
add_test(NAME test_qram_read_bb_round_trip COMMAND test_qram_read_bb_round_trip)
set_tests_properties(test_qram_read_bb_round_trip
    PROPERTIES LABELS "qram;qram-backend;lossy-reversibility")
