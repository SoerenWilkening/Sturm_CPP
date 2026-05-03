cmake_minimum_required(VERSION 3.16)
enable_language(C)

# ── tests/qtypes/ — backend-level qtypes free functions ───────────────────────
#
# Holds tests for `include/sturm/qtypes/*.hpp` pieces that are consumed by the
# LO compile-time rewrite pass (sturm-lo).  Mirrors the pattern established in
# tests/backend/: each target pulls in the shared M19 backend sources
# (context, control_stack, execute_gate, orkan_bridge) so the qint_t<W>
# operators can emit gates and simulate.
#
# The source list deliberately duplicates M19_SOURCES rather than importing it
# from the sibling tests/backend/CMakeLists.txt — these live in separate
# subdirectories and CMake scope rules would otherwise require promoting the
# variable to CACHE.

set(STURM_QTYPES_BACKEND_SOURCES
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
    # sturm-a3t4.4: lib_div_dsl / lib_mul_dsl now use uncompute_and for
    # the depth-1 lift, so any qtypes test that pulls these primitives
    # (divide_oop / mul_oop / lossy_oop) needs the uncompute_api
    # translation unit linked in.
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
)

# sturm-a3qp LO-1e — divide_oop out-of-place division free function.
add_executable(test_divide_oop
    test_divide_oop.cpp
    ${STURM_QTYPES_BACKEND_SOURCES}
)
target_include_directories(test_divide_oop PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_divide_oop PRIVATE cxx_std_20)
target_compile_definitions(test_divide_oop PRIVATE
    STURM_BACKEND_ENABLED=1
    STURM_MAX_QUBITS=17
)
target_link_libraries(test_divide_oop PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_divide_oop COMMAND test_divide_oop)
set_tests_properties(test_divide_oop PROPERTIES LABELS "qtypes;lossy-reversibility")

# sturm-ph6f.3 LO-2 backend — and_oop / and_oop_adj reversible Toffoli ladder
# dispatched into c_and_dsl. Mirrors the divide_oop fast-path / classical-path
# split: when at least one operand carries qubits the wrapper emits a per-bit
# CCX; otherwise (both qubits[0] < 0) the classical short-circuit body runs to
# preserve example_qint_arith / example_phase_abc_demo.
add_executable(test_and_oop
    test_and_oop.cpp
    ${STURM_QTYPES_BACKEND_SOURCES}
)
target_include_directories(test_and_oop PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_and_oop PRIVATE cxx_std_20)
target_compile_definitions(test_and_oop PRIVATE
    STURM_BACKEND_ENABLED=1
    STURM_MAX_QUBITS=12
)
target_link_libraries(test_and_oop PRIVATE orkan_headers)
add_test(NAME test_and_oop COMMAND test_and_oop)
set_tests_properties(test_and_oop PROPERTIES LABELS "qtypes;lossy-reversibility")

# sturm-ph6f.2 LO-2 backend — mul_oop / mul_oop_adj reversible shift-and-add
# multiplication network dispatched into lib_mul_dsl. Mirrors the divide_oop
# fast-path / classical-path split: when at least one operand carries qubits
# the wrapper allocates W qubits for tmp + W ancillas for the upper half of
# the 2W-bit multiplication result, runs lib_mul_dsl, and stashes the upper-W
# ancillas in a thread-local LIFO stack so the matching mul_oop_adj can
# uncompute and release them. Otherwise (both qubits[0] < 0) the classical
# short-circuit body runs to preserve example_qint_arith /
# example_phase_abc_demo.
add_executable(test_mul_oop
    test_mul_oop.cpp
    ${STURM_QTYPES_BACKEND_SOURCES}
)
target_include_directories(test_mul_oop PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_mul_oop PRIVATE cxx_std_20)
target_compile_definitions(test_mul_oop PRIVATE
    STURM_BACKEND_ENABLED=1
    STURM_MAX_QUBITS=16
)
target_link_libraries(test_mul_oop PRIVATE orkan_headers)
add_test(NAME test_mul_oop COMMAND test_mul_oop)
set_tests_properties(test_mul_oop PROPERTIES LABELS "qtypes;lossy-reversibility")

# sturm-ph6f.1 LO-2 backend — or_oop / or_oop_adj reversible per-bit OR ladder
# dispatched into lib_or_dsl. Mirrors the divide_oop fast-path / classical-path
# split: when at least one operand carries qubits the wrapper allocates W
# qubits for tmp and emits per-bit `tmp_i ^= a_i | b_i` (CX+CX+CCX); otherwise
# (both qubits[0] < 0) the classical short-circuit body runs to preserve
# example_qint_arith / example_phase_abc_demo.
add_executable(test_or_oop
    test_or_oop.cpp
    ${STURM_QTYPES_BACKEND_SOURCES}
)
target_include_directories(test_or_oop PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_or_oop PRIVATE cxx_std_20)
target_compile_definitions(test_or_oop PRIVATE
    STURM_BACKEND_ENABLED=1
    STURM_MAX_QUBITS=12
)
target_link_libraries(test_or_oop PRIVATE orkan_headers)
add_test(NAME test_or_oop COMMAND test_or_oop)
set_tests_properties(test_or_oop PROPERTIES LABELS "qtypes;lossy-reversibility")

# sturm-nwwu LO-2 backend — divide_oop_adj reversible non-restoring division
# adjoint dispatched into __lib_div_dsl_adj. Mirrors the mul_oop pattern: when
# at least one operand carries qubits the wrapper builds BitProxy views and
# calls __lib_div_dsl_adj which gate-reverses the forward network, returning
# q and r registers to |0…0> on the post-swap-undo state where a == q*b + r.
# Otherwise (both qubits[0] < 0) the bookkeeping-only inverse zeros q.value /
# r.value to keep the classical path coherent with the simulator result.
add_executable(test_divide_oop_adj
    test_divide_oop_adj.cpp
    ${STURM_QTYPES_BACKEND_SOURCES}
)
target_include_directories(test_divide_oop_adj PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_divide_oop_adj PRIVATE cxx_std_20)
target_compile_definitions(test_divide_oop_adj PRIVATE
    STURM_BACKEND_ENABLED=1
    STURM_MAX_QUBITS=17
)
target_link_libraries(test_divide_oop_adj PRIVATE orkan_headers)
add_test(NAME test_divide_oop_adj COMMAND test_divide_oop_adj)
set_tests_properties(test_divide_oop_adj PROPERTIES LABELS "qtypes;lossy-reversibility")

# sturm-8dzd RED-bar — sturm::swap(qint_t<W>&, qint_t<W>&) under a manually-
# pushed control stack at depth==1. Pins the LO-2 swap-under-WHEN contract
# epic sturm-8aq3 owns: depth==0 stays a zero-gate index relabel; depth>=1
# must lift to a per-bit Fredkin (3 CCX per bit). RED on main (current
# `sturm::swap(qint_t<W>&, qint_t<W>&)` in `include/sturm/qtypes/lossy_oop.hpp`
# emits zero gates regardless of control depth); GREEN after sturm-arce.
add_executable(test_when_swap_qint
    test_when_swap_qint.cpp
    ${STURM_QTYPES_BACKEND_SOURCES}
)
target_include_directories(test_when_swap_qint PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_when_swap_qint PRIVATE cxx_std_20)
target_compile_definitions(test_when_swap_qint PRIVATE
    STURM_BACKEND_ENABLED=1
    STURM_MAX_QUBITS=12
)
target_link_libraries(test_when_swap_qint PRIVATE orkan_headers)
add_test(NAME test_when_swap_qint COMMAND test_when_swap_qint)
set_tests_properties(test_when_swap_qint PROPERTIES LABELS "qtypes;lossy-reversibility")

# sturm-u9ge.5 (Beat A1) — qint_alias.hpp: frontend non-templated `qint`
# class with the load-bearing implicit `operator size_t()`. Pre-transpile
# safety-net header for the QRAM-subscript epic (PRD §4.1).
#
# This target deliberately does NOT pull the M19 backend sources nor
# define STURM_BACKEND_ENABLED — the A1 surface is header-only / pre-
# transpile, and the frontend `qint` does not interact with the gate-
# emission backend. We still need the qint.hpp include path (for the
# negative-SFINAE static_asserts on `qint_t<W>`), which transitively
# pulls qbool / qint_arith etc.; those are header-only too in the
# non-backend build profile.
add_executable(test_qint_alias
    test_qint_alias.cpp
)
target_include_directories(test_qint_alias PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qint_alias PRIVATE cxx_std_20)
add_test(NAME test_qint_alias COMMAND test_qint_alias)
set_tests_properties(test_qint_alias PROPERTIES LABELS "qtypes;qram-subscript")

# sturm-qjt7 — converting constructor `frontend::qint(const qint_t<W>&)`
# pins that `qint b = a[i];` parses against the production header for all
# three PRD §7 container shapes (std::array, C-array, pointer). Same
# header-only profile as test_qint_alias above.
add_executable(test_qint_alias_qint_t_init
    test_qint_alias_qint_t_init.cpp
)
target_include_directories(test_qint_alias_qint_t_init PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qint_alias_qint_t_init PRIVATE cxx_std_20)
add_test(NAME test_qint_alias_qint_t_init COMMAND test_qint_alias_qint_t_init)
set_tests_properties(test_qint_alias_qint_t_init PROPERTIES
    LABELS "qtypes;qram-subscript")

# sturm-u9ge.10 (Beat A2) — qint_alias_ops.hpp: arithmetic / compare /
# bitwise / compound-assign operator stubs for the frontend `qint` alias.
# Pre-transpile measure-then-classical body; post-transpile the C1 matcher
# rewrites the matched subscript shape so the stub bodies are unreachable
# (PRD §4.1 step 2 / plan §4 / A2).
#
# Same target shape as test_qint_alias above: header-only / pre-transpile,
# no backend sources, no STURM_BACKEND_ENABLED. The static-assertion
# harness in the test transitively includes qint.hpp for `qint_t<W>`,
# which is header-only in the non-backend build profile.
add_executable(test_qint_alias_ops
    test_qint_alias_ops.cpp
)
target_include_directories(test_qint_alias_ops PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qint_alias_ops PRIVATE cxx_std_20)
add_test(NAME test_qint_alias_ops COMMAND test_qint_alias_ops)
set_tests_properties(test_qint_alias_ops PROPERTIES LABELS "qtypes;qram-subscript")
