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

# sturm-65rs.2 (Beat A1 / sturm-qac.2) — member ops on `frontend::qint`:
# `operator=(int64_t)`, `operator[](size_t) const`, and
# `explicit operator int64_t() const`. Plan §4 / A1, PRD §4.1 member-ops
# bullet. Same header-only / pre-transpile profile as test_qint_alias.
add_executable(test_qint_alias_member_ops
    test_qint_alias_member_ops.cpp
)
target_include_directories(test_qint_alias_member_ops PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qint_alias_member_ops PRIVATE cxx_std_20)
add_test(NAME test_qint_alias_member_ops COMMAND test_qint_alias_member_ops)
set_tests_properties(test_qint_alias_member_ops PROPERTIES
    LABELS "qtypes;qram-subscript")

# ── sturm-v0db.7 (Wave 3 / W3.6 / A14) ────────────────────────────────────────
# Tree-grep audit gate: permanent regression guard against re-introduction
# of any deleted-infrastructure token from the qint-alias completion epic.
# After W3.4 (sturm-v0db.5 / G9) wiped the `qint_alias_detail` namespace,
# this test walks the configured source root at run time and hard-fails
# the build if any of the deleted tokens reappear in {include, tests,
# transpiler/src, transpiler/tests, examples}/**/*.{cpp,hpp,h}.
#
# Forbidden tokens (PRD §10.3.6 A14):
#   * \b(measure_to_int|g_measurement_count|
#         bump_measurement_count|reset_measurement_count)\b
#   * qint_alias_detail::
#
# Allow-listed (single-source documented exemption per PRD §10.3.6):
#   * transpiler/tests/test_sturm_gen_clean_unit.cpp — the G6 unit-level
#     scanner's positive-control fixture deliberately includes the
#     literal `qint_alias_detail::` substring to verify its detector arm
#     fires.
#
# SOURCE_ROOT plumbing mirrors Wave-2 G6's <sturm_gen_path.hpp>: a
# configure_file bakes ${CMAKE_SOURCE_DIR} into a constexpr constant in
# `test_qint_alias_audit_path.hpp` so the test compiles cleanly against
# the path it scans. A stale path errors at run time (directory does
# not exist), not silently at scan time.
#
# Header-only / pre-transpile profile: no LLVM, no backend sources, no
# STURM_BACKEND_ENABLED. The test depends on <filesystem> + <regex>
# from the C++20 standard library only.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/test_qint_alias_audit_path.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/test_qint_alias_audit_path.hpp"
    @ONLY)
add_executable(test_qint_alias_no_counter_infra
    test_qint_alias_no_counter_infra.cpp
)
target_include_directories(test_qint_alias_no_counter_infra PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_compile_features(test_qint_alias_no_counter_infra PRIVATE cxx_std_20)
add_test(NAME test_qint_alias_no_counter_infra
         COMMAND test_qint_alias_no_counter_infra)
set_tests_properties(test_qint_alias_no_counter_infra PROPERTIES
    LABELS "qtypes;qram-subscript;qac")

# ── sturm-v0db.6 (Wave 3 / W3.5 / A12) ────────────────────────────────────────
# IR-scan stub test: defense-in-depth gate that fires red if a future edit
# reintroduces a value-semantics body (or a call into the deleted W3.4
# detail namespace) on `sturm::frontend::qint`'s alias operators.
#
# Pipeline:
#   1. `fixture_qint_alias_stubs.cpp` is compiled with `clang++ -S
#      -emit-llvm -O0` by an `add_custom_command` whose output is
#      `${CMAKE_CURRENT_BINARY_DIR}/qint_alias_stubs.ll`. The fixture's
#      [[gnu::used]] entry points reference every alias operator + ctor
#      so the host compiler emits each body into the IR.
#   2. `test_qint_alias_stubs` (THIS executable) reads the resulting
#      `.ll` file at run time and runs four checks (PRD §10.4 A12 /
#      plan §29):
#        - per-function gep+load scan on qint members (excluding
#          `classical_value()`, which legitimately reads its single
#          field; free operators are not scanned because the SysV
#          x86-64 ABI coerces an 8-byte struct returned by-value
#          through a register, forcing a final gep+load that is a
#          pure ABI artifact, not value semantics);
#        - global call/invoke scan for any reference to the deleted
#          W3.4 detail namespace;
#        - IR file size sanity bound (catches umbrella-include drift).
#
# The IR generation invokes ${CMAKE_CXX_COMPILER} directly so the
# clang version pinned by the host-clang invariant (sturm-yial) is the
# one that produces the IR -- this matters because the mangled-name
# prefixes the IR scanner anchors on are Itanium-ABI specific and the
# pinned clang is the one whose ABI we test against.
#
# Header-only / pre-transpile profile: no LLVM dev headers, no backend
# translation units, no STURM_BACKEND_ENABLED. The IR-scan test parses
# `.ll` text with the standard library only.
set(_sturm_qint_alias_fixture_src
    "${CMAKE_CURRENT_SOURCE_DIR}/fixture_qint_alias_stubs.cpp")
set(_sturm_qint_alias_stubs_ll
    "${CMAKE_CURRENT_BINARY_DIR}/qint_alias_stubs.ll")

# Mirror the include plumbing that any header-only qtypes test
# relies on (cf. test_qint_alias above): the `${CMAKE_SOURCE_DIR}/include`
# tree carries the alias headers. (sturm-5jta: legacy compile-time pool
# cap removed — the qubit pool grows on demand.)
add_custom_command(
    OUTPUT  "${_sturm_qint_alias_stubs_ll}"
    COMMAND "${CMAKE_CXX_COMPILER}"
            -std=c++17 -S -emit-llvm -O0
            "-I${CMAKE_SOURCE_DIR}/include"
            "-I${CMAKE_BINARY_DIR}/include"
            -o "${_sturm_qint_alias_stubs_ll}"
            "${_sturm_qint_alias_fixture_src}"
    DEPENDS "${_sturm_qint_alias_fixture_src}"
    COMMENT "Generating qint_alias_stubs.ll for sturm-v0db.6 IR scan"
    VERBATIM)
# `add_custom_target` is the dependency anchor that the test executable
# links its `add_dependencies` against; the IR file itself is declared
# as `OUTPUT` of the custom command above so CMake can wire it in as
# `DEPENDS` here. (BYPRODUCTS only takes effect when the custom target
# carries its own COMMAND, which we deliberately avoid -- the actual
# build rule lives on `add_custom_command` per CMake best practice.)
add_custom_target(qint_alias_stubs_ll
    DEPENDS "${_sturm_qint_alias_stubs_ll}")

add_executable(test_qint_alias_stubs
    test_qint_alias_stubs.cpp
)
add_dependencies(test_qint_alias_stubs qint_alias_stubs_ll)
target_compile_features(test_qint_alias_stubs PRIVATE cxx_std_20)
target_compile_definitions(test_qint_alias_stubs PRIVATE
    "STURM_QINT_ALIAS_STUBS_LL_PATH=\"${_sturm_qint_alias_stubs_ll}\"")
add_test(NAME test_qint_alias_stubs COMMAND test_qint_alias_stubs)
set_tests_properties(test_qint_alias_stubs PROPERTIES
    LABELS "qtypes;qram-subscript;qac")

# ── sturm-zbzo (Frontend simpl. P2.a / G5) ────────────────────────────────
# Pool-growth gate after qubit-cap removal. After P2.a:
#   * The legacy cap macro / sentinel constant / abort path are gone.
#   * QubitPool has no cap; allocate() grows the pool on demand.
#   * BackendContext takes only (mode) — no max_qubits.
# This test acquires 1024 qubits (well past every former cap) and
# asserts no abort, all indices distinct, and clean release.
#
# Pulls the M19 backend sources because the test exercises
# BackendContext directly (sturm_backend_context_t {STURM_MODE_COUNT_ONLY}
# touches context.cpp's process-wide-default machinery transitively via
# the constructor).
add_executable(test_qubit_pool_growth
    test_qubit_pool_growth.cpp
    ${STURM_QTYPES_BACKEND_SOURCES}
)
target_include_directories(test_qubit_pool_growth PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qubit_pool_growth PRIVATE cxx_std_20)
target_compile_definitions(test_qubit_pool_growth PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_qubit_pool_growth PRIVATE orkan_headers)
add_test(NAME test_qubit_pool_growth COMMAND test_qubit_pool_growth)
set_tests_properties(test_qubit_pool_growth PROPERTIES
    LABELS "qtypes;frontend-simplification;qubit-cap-removal")
