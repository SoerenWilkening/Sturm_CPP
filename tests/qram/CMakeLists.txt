cmake_minimum_required(VERSION 3.16)
enable_language(C)

# ── tests/qram/ — runtime entry-point tests for the QRAM-subscript epic ──
#
# sturm-u9ge.13 (Beat D1) — counter-mode stub for `sturm::QRAM_read`
# across the three container shapes from PRD §7. Pinned by
# `tests/qram/test_qram_read_stub.cpp` against the production header
# `include/sturm/qram/qram_read.hpp` and the TU-private body
# `src/sturm/qram/qram_read.cpp`. Counter-mode body bumps a single
# `qram_read` thread-local counter; the test file itself defines
# `STURM_BACKEND_ENABLED 1` so it can call `invert<&QRAM_read<W>>()`
# (the `STURM_REGISTER_ADJOINT` enrolment in the header is backend-
# gated).
#
# sturm-2w6h.4 (Beat B2) — wiring `lib_qram_read_qrom_dsl` from
# `qram_read_qrom_impl` makes the public `QRAM_read` overloads emit a
# real gate stream when the test TU sets the backend macro. Backend
# sources (context, control_stack, execute_gate, orkan_bridge,
# uncompute_api) are linked here to satisfy the new symbol references
# (`sturm_get_thread_context`, `execute_gate`, etc.).
set(STURM_QRAM_BACKEND_SOURCES
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)

add_executable(test_qram_read_stub
    test_qram_read_stub.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/qram/qram_read.cpp
    ${STURM_QRAM_BACKEND_SOURCES}
)
target_include_directories(test_qram_read_stub PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qram_read_stub PRIVATE cxx_std_20)
target_link_libraries(test_qram_read_stub PRIVATE orkan_headers)
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

# ── sturm-2w6h.2 (Beat B1) — refactor impls to receive (a, n, i, b) ──
# Pins the refactor of `_qram_detail::qram_read_qrom_impl` and
# `qram_read_qreg_impl` from no-arg shims into templates parameterised
# on `(W)` taking `(const qint_t<W>* a, std::size_t n,
# const qint_t<W>& i, qint_t<W>& b)`. The three public `QRAM_read`
# overloads and the three `__QRAM_read_adj` overloads forward
# `(a, i, b)` (and `n`, inferred from `N` for array shapes) to the
# helper. Bodies remain counter-mode bumps in this beat — observable
# behaviour (umbrella `qram_read_count` + mask-OR dispatch) is
# unchanged. Plan `docs/plan_qram_backend.md` §5 B1.
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
# Pins the wiring of the split `qrom_read` / `qreg_read` telemetry
# counters (B0 surface, sturm-2w6h.1) through the QROM and qreg
# dispatch helpers. The QROM helper now bumps `current_sink()->qrom_read()`
# once per dispatched call; the qreg helper bumps
# `current_sink()->qreg_read()` once. The umbrella `qram_read()`
# sink hook continues to fire from the public `QRAM_read` entry-point
# (so existing telemetry-shaped tests stay green); the thread-local
# `qram::g_qram_read_count` continues to bump from the dispatch
# common helper. Plan `docs/plan_qram_backend.md` §5 B4.
add_executable(test_qram_telemetry_split
    test_qram_telemetry_split.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/qram/qram_read.cpp
    ${STURM_QRAM_BACKEND_SOURCES}
)
target_include_directories(test_qram_telemetry_split PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qram_telemetry_split PRIVATE cxx_std_20)
target_link_libraries(test_qram_telemetry_split PRIVATE orkan_headers)
add_test(NAME test_qram_telemetry_split COMMAND test_qram_telemetry_split)
set_tests_properties(test_qram_telemetry_split PROPERTIES LABELS "qram;qram-backend")
