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

# ── sturm-44bt.4 (Beat BB4) — first qreg-path gate-emission test ────
# Pins G1+G5 at the public `QRAM_read` site for the qreg arm: §4.4
# closed-form CCX/CNOT/X counts at (N=4, W=4), qreg_read=1 /
# qrom_read=0 / umbrella=1, statevector entanglement of b.bit(0)
# with a[3].bit(0) at (N=4, W=2, 17q).
add_executable(test_qram_read_bb_qreg_gates
    test_qram_read_bb_qreg_gates.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/qram/qram_read.cpp
)
target_compile_features(test_qram_read_bb_qreg_gates PRIVATE cxx_std_20)
target_link_libraries(test_qram_read_bb_qreg_gates PRIVATE sturm_backend_runtime)
add_test(NAME test_qram_read_bb_qreg_gates COMMAND test_qram_read_bb_qreg_gates)
set_tests_properties(test_qram_read_bb_qreg_gates
    PROPERTIES LABELS "qram;qram-backend")

# ── sturm-44bt.4 (Beat BB4) — pointer overload dispatch table ───────
# Pins the switch(ceil_log2_index(n)) table coverage from PRD §7:
# for n in {0,1,2,3,4,5,7,8,16,32,1023,1024}, asserts umbrella +
# split + forwarding trace. n=1025 fires the over-cap diagnose
# (sink-side counter). Compiled with NDEBUG so the diagnose's
# `assert(false)` is suppressed — we pin the RELEASE-mode behaviour.
# Both the test TU and the qram_read.cpp TU need NDEBUG so the
# assert defined in qram_read_n_over_cap_diagnose doesn't fire.
add_executable(test_qram_read_bb_pointer_dispatch
    test_qram_read_bb_pointer_dispatch.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/qram/qram_read.cpp
)
target_compile_features(test_qram_read_bb_pointer_dispatch PRIVATE cxx_std_20)
target_compile_definitions(test_qram_read_bb_pointer_dispatch PRIVATE NDEBUG)
target_link_libraries(test_qram_read_bb_pointer_dispatch PRIVATE sturm_backend_runtime)
add_test(NAME test_qram_read_bb_pointer_dispatch COMMAND test_qram_read_bb_pointer_dispatch)
set_tests_properties(test_qram_read_bb_pointer_dispatch
    PROPERTIES LABELS "qram;qram-backend")

# ── sturm-44bt.5 (Beat BB5) — power-of-2 padding (phantom-leaf transit)
# Pins PRD §6: for N not a power of 2, lib_qram_read_bb_dsl<W, N>
# instantiates at N' = 2^⌈log2 N⌉ and realises phantom leaves
# [N, N') as W-qubit |0⟩^W transit ancilla blocks at call entry.
# Statevector forward reads at (N=3, W=2) over i ∈ {0, 2, 3}:
# i=3 returns b=0 (phantom |0⟩^W). Gate count == (N'=4, W=2) §4.4
# closed form — phantom emits the same gate stream as a real
# quantum leaf.
add_executable(test_qram_read_bb_padding
    test_qram_read_bb_padding.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/qram/qram_read.cpp
)
target_compile_features(test_qram_read_bb_padding PRIVATE cxx_std_20)
target_link_libraries(test_qram_read_bb_padding PRIVATE sturm_backend_runtime)
add_test(NAME test_qram_read_bb_padding COMMAND test_qram_read_bb_padding)
set_tests_properties(test_qram_read_bb_padding
    PROPERTIES LABELS "qram;qram-backend")
