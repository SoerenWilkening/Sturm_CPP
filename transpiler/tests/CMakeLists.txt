# transpiler/tests/CMakeLists.txt — M4 test suite.
#
# Two test binaries:
#   test_io      — pure unit tests for sturm::transpile::{resolve_output_path,
#                  write_file, read_file}. No LibTooling, no subprocess.
#   test_driver  — end-to-end tests that invoke the compiled sturm-transpile
#                  binary. Uses STURM_TRANSPILE_BIN compile definition so the
#                  test does not hard-code a build-tree path.

cmake_minimum_required(VERSION 3.16)

# Shared io helper sources, compiled into each test binary that needs them.
set(STURM_TRANSPILE_IO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/io.cpp
)

# ── Unit test: io helpers ─────────────────────────────────────────────────────
add_executable(test_transpile_io
    test_io.cpp
    ${STURM_TRANSPILE_IO_SOURCES}
)
target_include_directories(test_transpile_io PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_compile_features(test_transpile_io PRIVATE cxx_std_20)
add_test(NAME test_transpile_io COMMAND test_transpile_io)
set_tests_properties(test_transpile_io PROPERTIES LABELS "transpiler")

# ── Unit test: FreshNameAllocator (PE-3) ────────────────────────────────────
# The fresh-name helper is a header-only class (transpiler/src/fresh_names.hpp)
# with no LibTooling or Clang dependency — just <string> — so the test compiles
# and links in under a second with no extra include paths.
add_executable(test_transpile_fresh_names
    test_fresh_names.cpp
)
target_include_directories(test_transpile_fresh_names PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_features(test_transpile_fresh_names PRIVATE cxx_std_20)
add_test(NAME test_transpile_fresh_names COMMAND test_transpile_fresh_names)
set_tests_properties(test_transpile_fresh_names PROPERTIES LABELS "transpiler")

# ── Unit test: Quantum IR (M6) ────────────────────────────────────────────────
# The IR depends only on clang::SourceLocation / SourceRange, which are
# header-inline, so the test compiles against the LLVM/Clang include dirs
# but does NOT need to link any LibTooling components. This keeps the test
# fast to build (< 1s) and runnable on any host with the Clang headers
# installed.
add_executable(test_transpile_qir
    test_qir.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qir.cpp
)
target_include_directories(test_transpile_qir PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
# Match the main binary's RTTI / definitions so the Clang headers see the
# exact same configuration they were compiled with.
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_transpile_qir PRIVATE -fno-rtti)
endif()
target_compile_features(test_transpile_qir PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_QIR NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_transpile_qir PRIVATE ${LLVM_DEFS_LIST_QIR})
add_test(NAME test_transpile_qir COMMAND test_transpile_qir)
set_tests_properties(test_transpile_qir PROPERTIES LABELS "transpiler")

# ── Unit test: DiagContext (PM3-0 + P-D) ────────────────────────────────────
# Pins the DiagContext public surface: the `getOrRegister` cache plus every
# `report_*` member. Drives a standalone `clang::DiagnosticsEngine` fronted
# by a counting `DiagnosticConsumer` so each report_* call is observed as a
# diagnostic landing in the sink at the expected severity. No MatchFinder,
# no AST — DiagContext talks only to DiagnosticsEngine, so the test binary
# links just `diag_context.cpp` plus `clangBasic` (for the engine's
# `Report` / `getCustomDiagID` out-of-line bodies) and `LLVMSupport` (for
# `IntrusiveRefCntPtr` / `StringRef` statics). Mirrors the shared-vs-static
# split every transpiler-adjacent test target uses.
#
# sturm-z2e8.4 / P-D extends the `report_*` family with five
# reversible-synthesis diagnostics; the test's `test_report_reversible_*`
# cases pin each new method as firing exactly one Error-severity report.
add_executable(test_transpile_diag_context
    test_diag_context.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/diag_context.cpp
)
target_include_directories(test_transpile_diag_context PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_transpile_diag_context PRIVATE -fno-rtti)
endif()
target_compile_features(test_transpile_diag_context PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_DIAG NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_transpile_diag_context PRIVATE
    ${LLVM_DEFS_LIST_DIAG})
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_transpile_diag_context PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_transpile_diag_context PRIVATE
        clangBasic
    )
    llvm_map_components_to_libnames(LLVM_LIBS_DIAG support)
    target_link_libraries(test_transpile_diag_context PRIVATE
        ${LLVM_LIBS_DIAG})
endif()
add_test(NAME test_transpile_diag_context COMMAND test_transpile_diag_context)
set_tests_properties(test_transpile_diag_context PROPERTIES
    LABELS "transpiler")

# ── Unit test: reversible_attribute (P-A / sturm-z2e8.2) ─────────────────────
# Phase P plan §2.1 P-A: pins `is_reversible(const FunctionDecl*)` for the
# `[[sturm::reversible]]` opt-in marker. The detection primitive consumes
# a `clang::AnnotateAttr` whose annotation string equals
# `"sturm::reversible"`; the tests exercise recognition on a plain free
# function, rejection of a typo (`"sturm::reversibel"`), and survival of
# template instantiation.
#
# The test drives its own lightweight ASTConsumer via
# `runToolOnCodeWithArgs` (same posture as test_alias_footprint / the
# other direct-ASTConsumer tests above) rather than routing through the
# MVP matcher harness. Linkage mirrors test_alias_footprint: shared
# libclang-cpp + libLLVM when available, fallback to the static
# per-component archives otherwise. Only `reversible_attribute.cpp` is
# linked from the transpiler side — the predicate is a free function
# over a decl and has no dependency on the matcher pool, the synthesis
# registry (P-B), or the diag context (P-D).
add_executable(test_reversible_attribute
    test_reversible_attribute.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/reversible_attribute.cpp
)
target_include_directories(test_reversible_attribute PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_reversible_attribute PRIVATE -fno-rtti)
endif()
target_compile_features(test_reversible_attribute PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_REVERSIBLE NATIVE_COMMAND
    "${LLVM_DEFINITIONS}")
target_compile_definitions(test_reversible_attribute PRIVATE
    ${LLVM_DEFS_LIST_REVERSIBLE})
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_reversible_attribute PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_reversible_attribute PRIVATE
        clangTooling
        clangASTMatchers
        clangFrontend
        clangRewrite
        clangBasic
        clangAST
        clangLex
        clangSerialization
        clangDriver
        clangParse
        clangSema
        clangAnalysis
        clangEdit
    )
    llvm_map_components_to_libnames(LLVM_LIBS_REVERSIBLE support)
    target_link_libraries(test_reversible_attribute PRIVATE
        ${LLVM_LIBS_REVERSIBLE})
endif()
add_test(NAME test_reversible_attribute COMMAND test_reversible_attribute)
set_tests_properties(test_reversible_attribute PROPERTIES
    LABELS "transpiler")

# ── Unit test: synthesis_registry (P-B / sturm-z2e8.3) ──────────────────────
# Phase P plan §2.1 P-B: pins the `SynthesisRegistry` container and its
# bridge to `RoutineRegistry` (PRD §9 Q2 — hand-registered adjoint wins).
# The registry is a pure data structure (no AST matcher binds to it), so
# the test is a standalone unit exerciser: it feeds synthetic
# `const clang::FunctionDecl*` pointers into the map, asserts on
# insert/lookup round-trip, conflict detection against `RoutineRegistry`,
# deterministic iteration order across insertions, and the null-FD
# guard on every public method.
#
# No LibTooling is required — the registry never dereferences its
# keys. Linkage mirrors `test_transpile_diag_context` (the other
# AST-free direct test): pull in `synthesis_registry.cpp` and
# `routine_registry.cpp` from the transpiler source tree, link the
# minimum LLVM / Clang needed to satisfy any transitive symbols, and
# register under the `transpiler` ctest label.
add_executable(test_synthesis_registry
    test_synthesis_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/synthesis_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/routine_registry.cpp
)
target_include_directories(test_synthesis_registry PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_synthesis_registry PRIVATE -fno-rtti)
endif()
target_compile_features(test_synthesis_registry PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_SYN_REG NATIVE_COMMAND
    "${LLVM_DEFINITIONS}")
target_compile_definitions(test_synthesis_registry PRIVATE
    ${LLVM_DEFS_LIST_SYN_REG})
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_synthesis_registry PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_synthesis_registry PRIVATE
        clangASTMatchers
        clangBasic
        clangAST
    )
    llvm_map_components_to_libnames(LLVM_LIBS_SYN_REG support)
    target_link_libraries(test_synthesis_registry PRIVATE
        ${LLVM_LIBS_SYN_REG})
endif()
add_test(NAME test_synthesis_registry COMMAND test_synthesis_registry)
set_tests_properties(test_synthesis_registry PROPERTIES
    LABELS "transpiler")

# ── Unit test: return_to_out_param (Q-A / sturm-5kgu.2) ─────────────────────
# Phase Q plan §2.2 Q-A: pins the out-param-twin source synthesis for
# reversible forwards whose body is a single `return <expr>;`. The tests
# run four positive fixtures (`return_to_out_param_{1..4}.cpp`) through
# `runToolOnCodeWithArgs`, locate the named forward FunctionDecl, invoke
# `synthesize_out_param_twin`, and compare the produced source text
# byte-for-byte against the sibling `.expected.cpp` golden file. Negative
# reject paths (NotReversible, non-quantum return, multi-statement body,
# forward declaration, null decl) are exercised inline without a fixture.
#
# Fixture directory is threaded in via STURM_RETURN_TO_OUT_PARAM_FIXTURES_DIR
# so the test reads goldens straight from the source tree — mirrors the
# `STURM_TRANSPILE_FIXTURES_DIR` pattern used by `test_transpile_skip`.
#
# Linkage pulls in `return_to_out_param.cpp` (the module under test) plus
# `reversible_attribute.cpp` (the `is_reversible` predicate Q-A consults
# on every input). Shared libclang-cpp + libLLVM when available, fallback
# to the per-component archives otherwise, same shape every other direct-
# ASTConsumer test in this file uses.
add_executable(test_return_to_out_param
    test_return_to_out_param.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/return_to_out_param.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/reversible_attribute.cpp
)
target_include_directories(test_return_to_out_param PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_return_to_out_param PRIVATE -fno-rtti)
endif()
target_compile_features(test_return_to_out_param PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_R2OP NATIVE_COMMAND
    "${LLVM_DEFINITIONS}")
target_compile_definitions(test_return_to_out_param PRIVATE
    ${LLVM_DEFS_LIST_R2OP}
    STURM_RETURN_TO_OUT_PARAM_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_return_to_out_param PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_return_to_out_param PRIVATE
        clangTooling
        clangASTMatchers
        clangFrontend
        clangRewrite
        clangBasic
        clangAST
        clangLex
        clangSerialization
        clangDriver
        clangParse
        clangSema
        clangAnalysis
        clangEdit
    )
    llvm_map_components_to_libnames(LLVM_LIBS_R2OP support)
    target_link_libraries(test_return_to_out_param PRIVATE
        ${LLVM_LIBS_R2OP})
endif()
add_test(NAME test_return_to_out_param COMMAND test_return_to_out_param)
set_tests_properties(test_return_to_out_param PROPERTIES
    LABELS "transpiler")

# ── Unit test: adjoint_emitter (R-1 / sturm-88d7.2) ─────────────────────────
# Phase R plan §2.3 R-A: pins the straight-line adjoint emission that
# walks a validated + normalized reversible forward body in reverse
# statement order and produces a sibling `__<fn>_adj` function. The
# tests hand-build `QOperation` vectors (no LibTooling needed for the
# pure-string entry point) and also exercise `emit_adjoint_for_decl`
# on a handful of `runToolOnCodeWithArgs` snippets to pin FD-based
# reject paths.
#
# Linkage pulls in `adjoint_emitter.cpp` (module under test) plus the
# dependencies it calls directly: `uncompute_pass.cpp` for
# `render_uncompute`, `reversible_attribute.cpp` for `is_reversible`,
# `plugin_registry.cpp` for the plugin::Registry destructor symbol
# referenced via `plugin_api.hpp`, and `qir.cpp` for the `QValueRef`
# equality operator referenced transitively from `qir.hpp`. Shared
# libclang-cpp + libLLVM when available, fallback to per-component
# archives otherwise — same shape every other direct-ASTConsumer
# test in this file uses.
add_executable(test_adjoint_emitter
    test_adjoint_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/adjoint_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/uncompute_pass.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/reversible_attribute.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/plugin_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qir.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/source_map.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/io.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/skip.cpp
)
target_include_directories(test_adjoint_emitter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_adjoint_emitter PRIVATE -fno-rtti)
endif()
target_compile_features(test_adjoint_emitter PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_ADJ_EMIT NATIVE_COMMAND
    "${LLVM_DEFINITIONS}")
target_compile_definitions(test_adjoint_emitter PRIVATE
    ${LLVM_DEFS_LIST_ADJ_EMIT})
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_adjoint_emitter PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_adjoint_emitter PRIVATE
        clangTooling
        clangASTMatchers
        clangFrontend
        clangRewrite
        clangBasic
        clangAST
        clangLex
        clangSerialization
        clangDriver
        clangParse
        clangSema
        clangAnalysis
        clangEdit
    )
    llvm_map_components_to_libnames(LLVM_LIBS_ADJ_EMIT support)
    target_link_libraries(test_adjoint_emitter PRIVATE
        ${LLVM_LIBS_ADJ_EMIT})
endif()
add_test(NAME test_adjoint_emitter COMMAND test_adjoint_emitter)
set_tests_properties(test_adjoint_emitter PROPERTIES
    LABELS "transpiler")

# ── Compile-only smoke: PM4-1 plugin_api.hpp ────────────────────────────────
# The PM4-1 public plugin surface lands as a pure header; the Registry
# method bodies are deferred to PM4-2's plugin_registry.cpp. This smoke
# test translation unit includes the header, instantiates a `Registry`
# (ctor is `= default`, so no implementation is linked), and exits 0 to
# prove the header parses cleanly under the same include + definition
# set every other transpiler test compiles under. It deliberately does
# NOT link `plugin_registry.cpp`, so any stray unresolved symbol inside
# the header (e.g. a mis-declared inline method) surfaces as a link
# error here — the intended gate for PM4-1 before PM4-2 lands.
add_executable(test_plugin_api_compile
    test_plugin_api_compile.cpp
)
target_include_directories(test_plugin_api_compile PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_plugin_api_compile PRIVATE -fno-rtti)
endif()
target_compile_features(test_plugin_api_compile PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_PLUGIN_API NATIVE_COMMAND
    "${LLVM_DEFINITIONS}")
target_compile_definitions(test_plugin_api_compile PRIVATE
    ${LLVM_DEFS_LIST_PLUGIN_API})
add_test(NAME test_plugin_api_compile COMMAND test_plugin_api_compile)
set_tests_properties(test_plugin_api_compile PROPERTIES LABELS "transpiler")

# ── Unit test: PM4-2 plugin_registry.cpp ────────────────────────────────────
# Exercises Registry::{register_matcher, register_op, invoke_all,
# find_render_fn} plus the `registrars()` Meyer singleton + `StaticRegistrar`
# link-time path. Collision detection is checked via fork-based subprocess
# assertions (the collision path calls `std::abort`, the test verifies the
# child terminates on SIGABRT). The test links `plugin_registry.cpp` and
# drags in `clangASTMatchers` because `invoke_all` takes a `MatchFinder&`
# whose ctor/dtor live in that library.
add_executable(test_plugin_registry
    test_plugin_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/plugin_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qir.cpp
)
target_include_directories(test_plugin_registry PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_plugin_registry PRIVATE -fno-rtti)
endif()
target_compile_features(test_plugin_registry PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_PLUGIN_REG NATIVE_COMMAND
    "${LLVM_DEFINITIONS}")
target_compile_definitions(test_plugin_registry PRIVATE
    ${LLVM_DEFS_LIST_PLUGIN_REG})
# MatchFinder's ctor/dtor live in libclangASTMatchers; we only need the
# minimum to construct + destruct one. Mirror the shared-vs-static split
# used elsewhere in this file.
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_plugin_registry PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_plugin_registry PRIVATE
        clangASTMatchers
        clangBasic
        clangAST
    )
    llvm_map_components_to_libnames(LLVM_LIBS_PLUGIN_REG support)
    target_link_libraries(test_plugin_registry PRIVATE
        ${LLVM_LIBS_PLUGIN_REG})
endif()
add_test(NAME test_plugin_registry COMMAND test_plugin_registry)
set_tests_properties(test_plugin_registry PROPERTIES LABELS "transpiler")

# ── Unit test: PM4-7 demo plugin ────────────────────────────────────────────
# Exercises the PM4-7 demo plugin under `examples/plugin_demo/` end-to-end
# via its `sturm_register_plugin_v1` entry point. The plugin's TU is
# compiled directly into the test binary (no dlopen hop — PM4-4's
# `load=<path>` is not merged yet, and per the PM4-7 issue the deliverable
# is the Registry API contract, not the runtime-load wiring). The test
# verifies `register_op` stores the render_fn under `"pm4.demo.tag"`,
# `find_render_fn` returns it, and the renderer emits the exact text
# `"    pm4_demo_tag_inverse(<q>);\n"` for a hand-built `QOperation`.
#
# Linkage mirrors test_plugin_registry: pulls in plugin_registry.cpp for
# the Registry method bodies and qir.cpp for the QOperation / QValueRef
# symbols referenced from the test's sample-op construction.
add_executable(test_plugin_demo
    test_plugin_demo.cpp
    ${CMAKE_SOURCE_DIR}/examples/plugin_demo/plugin_demo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/plugin_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qir.cpp
)
target_include_directories(test_plugin_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_plugin_demo PRIVATE -fno-rtti)
endif()
target_compile_features(test_plugin_demo PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_PLUGIN_DEMO NATIVE_COMMAND
    "${LLVM_DEFINITIONS}")
target_compile_definitions(test_plugin_demo PRIVATE
    ${LLVM_DEFS_LIST_PLUGIN_DEMO})
# Same clangBasic link line as test_plugin_registry: SourceLocation's
# ctor/dtor live in libclangBasic even though the symbol is opaque at
# the QOperation level. Wrapped in the shared-vs-static switch every
# transpiler-adjacent test target uses.
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_plugin_demo PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_plugin_demo PRIVATE
        clangBasic
    )
    llvm_map_components_to_libnames(LLVM_LIBS_PLUGIN_DEMO support)
    target_link_libraries(test_plugin_demo PRIVATE
        ${LLVM_LIBS_PLUGIN_DEMO})
endif()
add_test(NAME test_plugin_demo COMMAND test_plugin_demo)
set_tests_properties(test_plugin_demo PROPERTIES LABELS "transpiler")

# ── Unit test: PM4-6 dogfood migration (PB-1..PB-4) ─────────────────────────
# Pins the post-migration Registry API contract for the four Phase B
# qint_t compound-assign matchers (ADD/SUB/MUL/DIV_ASSIGN_CONST). The
# test binary links `matcher_qint_const.cpp` so its STURM_REGISTER_PLUGIN
# static-init fires during process startup, populating the Meyer's-
# singleton `registrars()` vector. The test drains that vector into a
# fresh `Registry`, runs `invoke_all` against a real `MatchFinder`, and
# asserts that four PB QOperations land in the QUnit with the identical
# QOpKind values pre- and post-migration.
#
# Linkage mirrors test_transpile_matcher (for runToolOnCodeWithArgs) plus
# plugin_registry.cpp for the Registry + Meyer's-singleton bodies. The
# in-tree source file set is the minimum the PB matchers transitively
# need: matcher_qint_const.cpp itself, qir.cpp for the IR types, and
# plugin_registry.cpp for the Registry methods.
add_executable(test_pm4_dogfood_registry
    test_pm4_dogfood_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qint_const.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/plugin_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qir.cpp
)
target_include_directories(test_pm4_dogfood_registry PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_pm4_dogfood_registry PRIVATE -fno-rtti)
endif()
target_compile_features(test_pm4_dogfood_registry PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_PM4_DOGFOOD NATIVE_COMMAND
    "${LLVM_DEFINITIONS}")
target_compile_definitions(test_pm4_dogfood_registry PRIVATE
    ${LLVM_DEFS_LIST_PM4_DOGFOOD})
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_pm4_dogfood_registry PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_pm4_dogfood_registry PRIVATE
        clangTooling
        clangASTMatchers
        clangFrontend
        clangRewrite
        clangBasic
        clangAST
        clangLex
        clangSerialization
        clangDriver
        clangParse
        clangSema
        clangAnalysis
        clangEdit
    )
    llvm_map_components_to_libnames(LLVM_LIBS_PM4_DOGFOOD support)
    target_link_libraries(test_pm4_dogfood_registry PRIVATE
        ${LLVM_LIBS_PM4_DOGFOOD})
endif()
add_test(NAME test_pm4_dogfood_registry COMMAND test_pm4_dogfood_registry)
set_tests_properties(test_pm4_dogfood_registry PROPERTIES LABELS "transpiler")

# ── Unit test: M8 uncompute synthesis pass ──────────────────────────────────
# Like the M6 QIR test, M8 uses only clang::SourceLocation — an opaque 32-bit
# wrapper — so the test does NOT need to link any LibTooling components. It
# compiles in under a second and runs in microseconds because the QUnit under
# test is constructed by hand. This is the whole reason M8 is separate from
# M7 and M9: ordering regressions can be pinpointed without a compiler in the
# loop.
add_executable(test_transpile_uncompute_pass
    test_uncompute_pass.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/uncompute_pass.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/source_map.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qir.cpp
    # PM4-3: `uncompute_pass.cpp` now calls into the plugin Registry via
    # `find_render_fn(kind_id)` when rendering a `QOpKind::PLUGIN` op.
    # Link `plugin_registry.cpp` so the Registry method bodies resolve at
    # link time. The synthesize-with-PLUGIN test cases in this file
    # construct a real Registry and call `register_op` on it.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/plugin_registry.cpp
)
target_include_directories(test_transpile_uncompute_pass PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_transpile_uncompute_pass PRIVATE -fno-rtti)
endif()
target_compile_features(test_transpile_uncompute_pass PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_UNCOMPUTE NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_transpile_uncompute_pass PRIVATE
    ${LLVM_DEFS_LIST_UNCOMPUTE})
# PM2-3: `uncompute_pass.cpp` now calls into `format_line_directive` (from
# `source_map.cpp`), which in turn reaches `clang::SourceManager::isIn
# MainFile` / `getPresumedLoc` from `libclangBasic`. The helper is
# gated on `sm != nullptr` so every hand-built test case in this file
# avoids the actual call, but the symbols still need to resolve at link
# time. Pull in `libclangBasic` (and the `llvm::DisableABIBreakingChecks`
# weak symbol it references) via the shared-vs-static path mirrored from
# every other transpiler test target.
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_transpile_uncompute_pass PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_transpile_uncompute_pass PRIVATE
        clangBasic
    )
    llvm_map_components_to_libnames(LLVM_LIBS_UNCOMPUTE support)
    target_link_libraries(test_transpile_uncompute_pass PRIVATE
        ${LLVM_LIBS_UNCOMPUTE})
endif()
add_test(NAME test_transpile_uncompute_pass
         COMMAND test_transpile_uncompute_pass)
set_tests_properties(test_transpile_uncompute_pass PROPERTIES
    LABELS "transpiler")

# ── Unit test: PM5-4 alias footprint extractor ──────────────────────────────
# Pins the `sturm::transpile::detail::footprint()` + `may_overlap()` API
# (declared in `sturm/transpile/alias.hpp`, implemented in
# `transpiler/src/alias.cpp`) across the fourteen-case matrix from
# `docs/implementation_plan_transpiler_phase_m_pm5.md` §10 (Verification):
# bare qbool, bare qint_t<W> for W ∈ {1, 4, 8}, BitProxy constant index
# (in-range + out-of-range fallback), BitProxy non-constant index
# (loop-induction-variable fallback), cross-decl disjointness for both
# qbool and qint_t<4>, same-decl partial overlap, same-decl disjoint
# bits, two `q[k]` with identical literal index, `f(q, q)`
# USER_ROUTINE double-reference aliasing (§12 Sharp edge 6), dependent-
# type universal sentinel fallback (§4 ladder step 5 / §12 Sharp edge 3),
# and universal-vs-concrete overlap wildcard reflex.
#
# The test drives `footprint()` through its own lightweight ASTConsumer
# pipeline (same posture as test_matcher_reader_count / test_matcher_
# scope_kind) rather than routing through the MVP OR matcher harness.
# The alias extractor is independent of any matcher registration, so the
# direct ASTConsumer path isolates the contract. Linkage mirrors
# test_transpile_consumer: shared libclang-cpp + libLLVM when available,
# fallback to the static per-component archives otherwise. `alias.cpp`
# is the only project source we link — the test does not touch the
# Registry, the matcher TUs, or the uncompute pass.
add_executable(test_alias_footprint
    test_alias_footprint.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/alias.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qir.cpp
)
target_include_directories(test_alias_footprint PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_alias_footprint PRIVATE -fno-rtti)
endif()
target_compile_features(test_alias_footprint PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_ALIAS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_alias_footprint PRIVATE
    ${LLVM_DEFS_LIST_ALIAS})
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_alias_footprint PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_alias_footprint PRIVATE
        clangTooling
        clangASTMatchers
        clangFrontend
        clangRewrite
        clangBasic
        clangAST
        clangLex
        clangSerialization
        clangDriver
        clangParse
        clangSema
        clangAnalysis
        clangEdit
    )
    llvm_map_components_to_libnames(LLVM_LIBS_ALIAS support)
    target_link_libraries(test_alias_footprint PRIVATE ${LLVM_LIBS_ALIAS})
endif()
add_test(NAME test_alias_footprint COMMAND test_alias_footprint)
set_tests_properties(test_alias_footprint PROPERTIES LABELS "transpiler")

# ── Unit test: PM5-7 peephole reorder matcher ───────────────────────────────
# Pins `register_peephole_reorder_matcher` (PM5-5, exported from
# `transpiler/src/matcher_peephole_reorder.cpp`) across the six
# fixtures listed in `docs/implementation_plan_transpiler_phase_m_pm5.md`
# §10 (Verification) / §12 (Sharp edges) / bd issue `sturm-u655.8`:
#
#   (1) disjoint-qbool         — reorder accepts.
#   (2) disjoint-qint-bits     — reorder accepts (distinct qint decl).
#   (3) overlap-rejected       — reorder refuses (B writes A's result).
#   (4) BitProxy-const         — reorder accepts (disjoint-decl qint).
#   (5) BitProxy-nonconst      — reorder refuses (universal sentinel).
#   (6) plugin-op-refused      — reorder refuses (opaque plugin op).
#
# Plus additional gate coverage pinning the §12 sharp-edges matrix:
# USER_ROUTINE refusal, hoist-boundary bail, fused-range bail,
# eliminated-range bail, reader-count gate, BranchBody scope-kind
# bail, non-AND A kind, non-synthetic A name, non-XOR_ASSIGN C kind,
# universal-sentinel A.result, mismatched consumer (C's operand name
# != A's result), and the <3 ops scope bail.
#
# The test drives the matcher through its own lightweight ASTConsumer
# pipeline (same posture as test_alias_footprint / test_matcher_
# reader_count): parse a tiny snippet, walk the AST for real VarDecl /
# CompoundStmt locations, pre-populate `unit.scopes` with the
# QOperation triple the matcher should observe, then invoke matchAST
# to trigger the matcher's translationUnitDecl() anchor and
# onEndOfTranslationUnit() terminal phase. This isolates the
# matcher's decision logic from the PJ-1d / PJ-3d / PJ-4a producers
# that would normally populate the scope; the PM5-8 end-to-end
# snapshot exercises the producers + matcher together.
#
# Linkage mirrors test_alias_footprint: shared libclang-cpp + libLLVM
# when available, fallback to the static per-component archives
# otherwise. We link `matcher_peephole_reorder.cpp` + `alias.cpp` +
# `source_map.cpp` (for format_line_directive) + `qir.cpp`.
add_executable(test_matcher_peephole_reorder
    test_matcher_peephole_reorder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_peephole_reorder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/alias.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/source_map.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qir.cpp
)
target_include_directories(test_matcher_peephole_reorder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_matcher_peephole_reorder PRIVATE -fno-rtti)
endif()
target_compile_features(test_matcher_peephole_reorder PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_REORDER NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_matcher_peephole_reorder PRIVATE
    ${LLVM_DEFS_LIST_REORDER})
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_matcher_peephole_reorder PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_matcher_peephole_reorder PRIVATE
        clangTooling
        clangASTMatchers
        clangFrontend
        clangRewrite
        clangBasic
        clangAST
        clangLex
        clangSerialization
        clangDriver
        clangParse
        clangSema
        clangAnalysis
        clangEdit
    )
    llvm_map_components_to_libnames(LLVM_LIBS_REORDER support)
    target_link_libraries(test_matcher_peephole_reorder PRIVATE
        ${LLVM_LIBS_REORDER})
endif()
add_test(NAME test_matcher_peephole_reorder
         COMMAND test_matcher_peephole_reorder)
set_tests_properties(test_matcher_peephole_reorder PROPERTIES
    LABELS "transpiler")

# ── Unit test: M9 C++ emitter ────────────────────────────────────────────────
# The emitter test invokes a real Clang SourceManager + Rewriter so it
# mirrors the matcher test's linkage: shared libclang-cpp + libLLVM when
# available, fallback to the static per-component archives otherwise.
add_executable(test_transpile_emitter
    test_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/source_map.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qbool_bitwise.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qbool_assign.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qint_const.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qint_qint.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qint_compare.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/uncompute_pass.cpp
    # PM4-3: uncompute_pass.cpp now references `Registry::find_render_fn`
    # from the plugin TU. Link plugin_registry.cpp so the Registry method
    # bodies resolve at link time.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/plugin_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qir.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/io.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/skip.cpp
)
target_include_directories(test_transpile_emitter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_transpile_emitter PRIVATE -fno-rtti)
endif()
target_compile_features(test_transpile_emitter PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_EMITTER NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_transpile_emitter PRIVATE
    ${LLVM_DEFS_LIST_EMITTER})
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_transpile_emitter PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_transpile_emitter PRIVATE
        clangTooling
        clangASTMatchers
        clangFrontend
        clangRewrite
        clangBasic
        clangAST
        clangLex
        clangSerialization
        clangDriver
        clangParse
        clangSema
        clangAnalysis
        clangEdit
    )
    llvm_map_components_to_libnames(LLVM_LIBS_EMITTER support)
    target_link_libraries(test_transpile_emitter PRIVATE ${LLVM_LIBS_EMITTER})
endif()
add_test(NAME test_transpile_emitter COMMAND test_transpile_emitter)
set_tests_properties(test_transpile_emitter PROPERTIES LABELS "transpiler")

# ── Unit test: M7 AST matcher ────────────────────────────────────────────────
# This test drives the `register_or_matcher` helper through a real Clang
# tooling invocation (runToolOnCodeWithArgs). It therefore needs to link
# against the same LibTooling components as sturm-transpile itself.
add_executable(test_transpile_matcher
    test_matcher.cpp
    test_matcher_harness.cpp
    test_matcher_mvp.cpp
    test_matcher_qint_const.cpp
    test_matcher_qint_qint.cpp
    test_matcher_qint_compare.cpp
    test_matcher_when_lift.cpp
    test_matcher_when_nested.cpp
    test_matcher_ph1_scope.cpp
    test_matcher_ph2_brace_wrap.cpp
    test_matcher_ph3_outer_var.cpp
    # Phase N PN-5: qbool(p) prep diagnostic matcher.
    test_matcher_pn5_qbool_prep.cpp
    # Phase N PN-2: rotation matcher (theta / phi +=/-=).
    test_matcher_pn_rotation.cpp
    test_routine_registry.cpp
    test_matcher_user_routine.cpp
    test_matcher_output_class.cpp
    test_matcher_scope_kind.cpp
    test_matcher_reader_count.cpp
    test_matcher_loop_invariant.cpp
    test_matcher_hoist_invariant.cpp
    test_matcher_ccnot_fuse.cpp
    test_matcher_dead_ancilla.cpp
    # PM2-2: Phase E compound-matcher `#line` directive emission tests.
    test_matcher_pm2_compound_line.cpp
    # PM2-3: M8 uncompute-synthesis `#line` directive emission tests.
    test_matcher_pm2_uncompute_line.cpp
    # PM2-4: QReplacement `#line` directive emission for ccnot-fuse +
    # WHEN-lift tests.
    test_matcher_pm2_replacement_line.cpp
    # PM2-5: hoist `#line` attribution policy — hoisted op emits #line
    # pointing at the ORIGINAL in-loop stmt_range.getBegin() location,
    # NOT the hoist_to_override target (post-loop close brace).
    test_matcher_pm2_hoist_line.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qbool_bitwise.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qbool_assign.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qbool_compound.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qint_const.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qint_qint.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qint_compare.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_when_lift.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_when_nested.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_brace_wrap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_outer_var_guard.cpp
    # Phase N PN-5: qbool(p) prep diagnostic matcher TU. Linked so the
    # matcher unit-test binary resolves `register_qbool_prep_matcher`
    # and the `*_for_test` counter helpers.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qbool_prep.cpp
    # Phase N PN-2: rotation matcher TU. Linked so the matcher
    # unit-test binary resolves `register_theta_add_matcher` and its
    # three siblings. The `STURM_REGISTER_PLUGIN(PNRotationPlugin)` at
    # the bottom of the TU also fires during static init here, which
    # is harmless — the test harness constructs a local MatchFinder
    # and registers the matchers through the free-function path, not
    # through the Registry drain (mirrors the PB dogfood test pattern
    # in `test_matcher_qint_const.cpp`).
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_rotation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_hoist_invariant.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_ccnot_fuse.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_dead_ancilla.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/routine_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_user_routine.cpp
    # PM3-0 / PM3-2 / PM3-3: matcher_outer_var_guard.cpp and
    # matcher_user_routine.cpp both call into DiagContext; link its
    # TU so the test binary resolves `report_outer_var_mutation` and
    # `report_missing_adjoint`.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/diag_context.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qir.cpp
    # PM2-2: `matcher_qbool_compound.cpp` now calls
    # `format_line_directive` from `emitter.cpp` to prefix each
    # synthesized `__stu_tN` decl with a `#line` directive.  Link the
    # emitter TU so this test binary resolves the symbol; the helper's
    # only LLVM / Clang dependencies (`SourceManager`, `PresumedLoc`)
    # are already satisfied by the existing clangBasic link line.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/emitter.cpp
    # PM2-3: `format_line_directive` moved from emitter.cpp into its own TU
    # `source_map.cpp` so test_transpile_uncompute_pass (no Rewriter
    # linkage) can resolve just that symbol. Link it here so the PM2-2
    # compound-matcher tests keep resolving `format_line_directive` via
    # the shared-source-map TU.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/source_map.cpp
    # emitter.cpp transitively includes io.hpp / skip.hpp / uncompute_pass.hpp
    # so their implementation TUs must be on the link line too.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/io.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/skip.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/uncompute_pass.cpp
    # PM4-3: uncompute_pass.cpp now references `Registry::find_render_fn`
    # from the plugin TU. Link plugin_registry.cpp so Registry method
    # bodies resolve at link time (the in-tree matcher tests never
    # construct a `QOpKind::PLUGIN` op, so the branch stays dead — but
    # the symbol must still resolve).
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/plugin_registry.cpp
)
target_include_directories(test_transpile_matcher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_transpile_matcher PRIVATE -fno-rtti)
endif()
target_compile_features(test_transpile_matcher PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_MATCHER NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_transpile_matcher PRIVATE
    ${LLVM_DEFS_LIST_MATCHER})
# Mirror the main binary's shared-vs-static LLVM link discipline so we do
# not double-register cl::opt tables (see the comment in the main CMake).
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_transpile_matcher PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_transpile_matcher PRIVATE
        clangTooling
        clangASTMatchers
        clangFrontend
        clangRewrite
        clangBasic
        clangAST
        clangLex
        clangSerialization
        clangDriver
        clangParse
        clangSema
        clangAnalysis
        clangEdit
    )
    llvm_map_components_to_libnames(LLVM_LIBS_MATCHER support)
    target_link_libraries(test_transpile_matcher PRIVATE ${LLVM_LIBS_MATCHER})
endif()
add_test(NAME test_transpile_matcher COMMAND test_transpile_matcher)
set_tests_properties(test_transpile_matcher PROPERTIES LABELS "transpiler")

# ── Unit + integration test: M5 skip / idempotency ──────────────────────────
# Pure string helpers plus a subprocess-driven round-trip over the two
# fixtures in tests/fixtures/. The subprocess leg reuses the same
# STURM_TRANSPILE_BIN macro the driver test does; the fixtures directory
# is threaded in as STURM_TRANSPILE_FIXTURES_DIR so the test can find the
# source-tree fixtures without guessing build-tree paths.
add_executable(test_transpile_skip
    test_skip.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/skip.cpp
)
target_include_directories(test_transpile_skip PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_compile_features(test_transpile_skip PRIVATE cxx_std_20)
target_compile_definitions(test_transpile_skip PRIVATE
    STURM_TRANSPILE_BIN="$<TARGET_FILE:sturm-transpile>"
    STURM_TRANSPILE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
add_dependencies(test_transpile_skip sturm-transpile)
add_test(NAME test_transpile_skip COMMAND test_transpile_skip)
set_tests_properties(test_transpile_skip PROPERTIES LABELS "transpiler")

# ── Unit test: shared TranspileConsumer (PM1-1) ─────────────────────────────
# The PM1-1 extraction pulls TranspileConsumer + its matcher registration
# pool out of main.cpp. This test pins the new EmissionMode boundary:
# StandaloneFile writes a headered file to disk; Plugin stashes the
# rewritten buffer on the consumer without touching disk or prepending
# the M5 sentinel.
add_executable(test_transpile_consumer
    test_transpile_consumer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/transpile_consumer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/diag_context.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qbool_bitwise.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qbool_assign.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qbool_compound.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qint_const.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qint_qint.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qint_compare.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_when_lift.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_when_nested.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_when_operand_mutation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_brace_wrap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_outer_var_guard.cpp
    # Phase N PN-5: qbool(p) prep diagnostic matcher TU. Linked here so
    # `test_transpile_consumer` resolves the symbol through the shared
    # TranspileConsumer registration path.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qbool_prep.cpp
    # Phase N PN-2: rotation matcher TU. Linked here so
    # `test_transpile_consumer` picks up the `STURM_REGISTER_PLUGIN`
    # static-init for `PNRotationPlugin` — the consumer's Registry
    # drain registers the four rotation matchers against its own
    # `finder_` just like it does for the PB dogfood plugin.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_rotation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_hoist_invariant.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_ccnot_fuse.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_dead_ancilla.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_dropped_quantum_return.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_quantum_to_classical_cond.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/routine_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_user_routine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/uncompute_pass.cpp
    # PM4-3: TranspileConsumer now owns a per-consumer plugin Registry
    # and threads it into `synthesize()`. Link plugin_registry.cpp so
    # the Registry method bodies (register_op, find_render_fn,
    # invoke_all, runtime_registrars, registrars) resolve at link time.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/plugin_registry.cpp
    # PM5-6: TranspileConsumer now calls register_peephole_reorder_matcher
    # at the tail of its registration block (LAST, after
    # register_hoist_invariant_matcher). The reorder matcher consumes
    # the alias extractor (`sturm/transpile/alias.hpp`). Link both TUs
    # so the callback symbols resolve at link time; the Phase J fixtures
    # this test harness exercises stay byte-identical because the
    # reorder matcher's Gate 1..4 guards refuse every pre-Phase-M
    # fixture (no synthetic __stu_t / xor-assign triple matches).
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/alias.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_peephole_reorder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/source_map.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/io.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/skip.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qir.cpp
)
target_include_directories(test_transpile_consumer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_transpile_consumer PRIVATE -fno-rtti)
endif()
target_compile_features(test_transpile_consumer PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_CONSUMER NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_transpile_consumer PRIVATE
    ${LLVM_DEFS_LIST_CONSUMER})
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_transpile_consumer PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_transpile_consumer PRIVATE
        clangTooling
        clangASTMatchers
        clangFrontend
        clangRewrite
        clangBasic
        clangAST
        clangLex
        clangSerialization
        clangDriver
        clangParse
        clangSema
        clangAnalysis
        clangEdit
    )
    llvm_map_components_to_libnames(LLVM_LIBS_CONSUMER support)
    target_link_libraries(test_transpile_consumer PRIVATE
        ${LLVM_LIBS_CONSUMER})
endif()
add_test(NAME test_transpile_consumer COMMAND test_transpile_consumer)
set_tests_properties(test_transpile_consumer PROPERTIES LABELS "transpiler")

# ── Integration test: plugin load (PM1-3) ────────────────────────────────────
# Shells out to the host clang++ with -fplugin=$<TARGET_FILE:...> against
# trivial non-Sturm TUs. Verifies the plugin loads, parses cleanly,
# honours -verbose and -dump-to=<path>, ignores unknown future flags,
# and rejects an empty dump-to= path. The compile-time macro
# STURM_PLUGIN_PATH is the generator expression for the plugin .so /
# .dylib; STURM_PLUGIN_CLANGXX is the clang++ driver located at
# configure time next to the Clang version we linked against.
find_program(STURM_PLUGIN_CLANGXX_BIN
    NAMES "clang++-${LLVM_VERSION_MAJOR}" "clang++"
    HINTS "${LLVM_TOOLS_BINARY_DIR}"
    NO_DEFAULT_PATH)
if(STURM_PLUGIN_CLANGXX_BIN)
    add_executable(test_plugin_load
        test_plugin_load.cpp
    )
    target_compile_features(test_plugin_load PRIVATE cxx_std_20)
    target_compile_definitions(test_plugin_load PRIVATE
        STURM_PLUGIN_PATH="$<TARGET_FILE:sturm-transpile-plugin>"
        STURM_PLUGIN_CLANGXX="${STURM_PLUGIN_CLANGXX_BIN}"
        # PM4-4: path to the PM4-7 demo plugin MODULE library used as
        # the runtime-dlopen fixture in the `load=<path>` gate. The demo
        # plugin target is defined in examples/plugin_demo/CMakeLists.txt
        # and lives under build/examples/plugin_demo/libsturm-pm4-demo-
        # plugin.{so,dylib}.
        STURM_PM4_DEMO_PLUGIN_PATH="$<TARGET_FILE:sturm-pm4-demo-plugin>"
    )
    # PM4-3: when STURM_PM4_LINK_DEMO=ON, the demo plugin is baked into
    # sturm-transpile-plugin.so at link-time, so the gate_load_demo_
    # plugin_succeeds test (which does `load=<demo>`) will hit the
    # post-PM4-3 duplicate-kind_id abort during the consumer's Registry
    # drain. Thread the configuration into the binary so the test gate
    # can weaken its happy-path assertion to "abort with the
    # documented collision line" under that config.
    if(STURM_PM4_LINK_DEMO)
        target_compile_definitions(test_plugin_load PRIVATE
            STURM_PM4_LINK_DEMO_ENABLED=1)
    endif()
    add_dependencies(test_plugin_load sturm-transpile-plugin
                                       sturm-pm4-demo-plugin)
    add_test(NAME test_plugin_load COMMAND test_plugin_load)
    set_tests_properties(test_plugin_load PROPERTIES LABELS "transpiler")
else()
    message(STATUS "sturm-transpile: test_plugin_load skipped "
                   "(clang++ not found next to LLVM_TOOLS_BINARY_DIR)")
endif()

# ── Integration test: nested plugin codegen (PM1-4) ─────────────────────────
# The PM1-4 gate: the plugin's ExecuteAction runs a nested
# CompilerInvocation + EmitObjAction over the rewritten buffer so one
# clang++ -fplugin command produces the same .o the legacy two-step
# (sturm-transpile, then clang++ -c) would have produced. We compare
# llvm-objdump -d disassembly for three fixtures spanning Phase E / H / J.
find_program(STURM_PLUGIN_OBJDUMP_BIN
    NAMES "llvm-objdump-${LLVM_VERSION_MAJOR}" "llvm-objdump"
    HINTS "${LLVM_TOOLS_BINARY_DIR}"
    NO_DEFAULT_PATH)
if(STURM_PLUGIN_CLANGXX_BIN AND STURM_PLUGIN_OBJDUMP_BIN)
    add_executable(test_plugin_nested
        test_plugin_nested.cpp
    )
    target_compile_features(test_plugin_nested PRIVATE cxx_std_20)
    target_compile_definitions(test_plugin_nested PRIVATE
        STURM_PLUGIN_PATH="$<TARGET_FILE:sturm-transpile-plugin>"
        STURM_PLUGIN_CLANGXX="${STURM_PLUGIN_CLANGXX_BIN}"
        STURM_TRANSPILE_BIN="$<TARGET_FILE:sturm-transpile>"
        STURM_PLUGIN_OBJDUMP="${STURM_PLUGIN_OBJDUMP_BIN}"
        STURM_PLUGIN_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples"
        STURM_PLUGIN_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/include"
    )
    add_dependencies(test_plugin_nested
        sturm-transpile-plugin sturm-transpile)
    add_test(NAME test_plugin_nested COMMAND test_plugin_nested)
    set_tests_properties(test_plugin_nested PROPERTIES LABELS "transpiler")
else()
    message(STATUS "sturm-transpile: test_plugin_nested skipped "
                   "(clang++ or llvm-objdump not found next to "
                   "LLVM_TOOLS_BINARY_DIR)")
endif()

# ── PM4-8 smoke: runtime-dlopen path via `PLUGINS` CMake argument ───────────
# End-to-end gate for the `add_quantum_executable(... PLUGINS
# $<TARGET_FILE:sturm-pm4-demo-plugin>)` pipeline added in PM4-5
# (`sturm-4oyr.6`). The smoke has two halves:
#
#   (1) A user fixture target `pm4_smoke_dlopen_fixture` compiled
#       through `add_quantum_executable(... PLUGINS
#       $<TARGET_FILE:sturm-pm4-demo-plugin>)`. This is the PRIMARY
#       artifact the PM4-8 contract asks for — if the `PLUGINS` cc1
#       arg pattern ever regressed (typo in `-Xclang` token order,
#       wrong generator expression resolution, `load=<path>` mishandled
#       by `plugin.cpp`'s `ParseArgs`, etc.), this target would fail to
#       compile and a `ctest -R pm4_smoke_dlopen` run would never even
#       reach the `pm4_smoke_dlopen` binary's `main()`.
#
#   (2) A test binary `pm4_smoke_dlopen` that:
#         - confirms the fixture target's `.o` + dump-to sibling are on
#           disk (proof the PLUGINS-driven compile ran to completion);
#         - replays a `verbose + load=<demo>` compile against a scratch
#           non-Sturm TU and asserts the demo's `pm4.demo.tag` kind_id
#           shows up in the host's probe Registry trace;
#         - conditionally checks the rewritten buffer for the
#           `pm4_demo_tag_inverse(` sentinel (deferred today because
#           PM4-3 / `sturm-4oyr.4` has not merged the
#           `QOpKind::PLUGIN` dispatch — see the test binary's
#           file-level comment for the handoff plan).
#
# The ctest case itself is wired through `pm4_smoke_dlopen.cmake` so
# failure diagnostics are captured + surfaced by the same relay
# posture `pm4_smoke_linktime` uses.
#
# Fixture target: `pm4_smoke_dlopen_fixture` ─────────────────────────────
# Source: `transpiler/tests/fixtures/pm4_smoke_dlopen_fixture.cpp`
# (a minimal `int main() { return 0; }` TU — no `sturm::qbool` ops, so
# the in-tree matcher pool fires zero times and the rewritten buffer
# is the input verbatim + the PM1-7 `AUTO-GENERATED` header). The
# fixture's sole purpose is to drive `add_quantum_executable(...
# PLUGINS <demo>)` end-to-end; the in-tree matcher pool's behaviour
# is pinned by every other transpiler test under this directory.
#
# Why `add_quantum_executable` rather than a manual `add_executable`
# -------------------------------------------------------------------
# The PM4-8 issue description explicitly requires "user code must be
# compiled via `add_quantum_executable(... PLUGINS $<TARGET_FILE:
# sturm-pm4-demo-plugin>)`". Threading the helper here — instead of
# shelling out to clang++ with a hand-spelled `-Xclang ... load=`
# pattern — pins the PUBLIC build-system surface, not just the
# underlying cc1 arg list. A regression in the helper's keyword
# parsing, the `PLUGINS` multi-value plumbing, or the per-source
# `COMPILE_OPTIONS` append would be caught here. The `verbose` replay
# in gate B of the test binary exercises the plugin-side leg
# separately against a scratch source.
#
# The `add_quantum_executable` helper builds the target under
# `plugin` mode by default. The `plugin` mode path (documented in
# `cmake/SturmTranspile.cmake:65-72`) is exactly what the PM4-5
# `PLUGINS` argument is defined against — `dump` mode would fail the
# helper with a FATAL_ERROR because PLUGINS has no cc1 slot in the
# standalone driver path. This test therefore assumes default mode;
# a build reconfigured with `-DSTURM_TRANSPILE_MODE=dump` would
# silently exclude the PM4-8 CTest at configure time.
#
# `STURM_PLUGIN_CLANGXX_BIN` is also required because the test's
# `gate_verbose_load_observes_kind_id` shells out to clang++ for the
# verbose-replay leg. The variable is set by `find_program` in the
# `test_plugin_load` block above; if clang++ is not reachable next to
# `LLVM_TOOLS_BINARY_DIR`, both `test_plugin_load` and
# `pm4_smoke_dlopen` are skipped the same way.
#
# PM4-3: under `STURM_PM4_LINK_DEMO=ON`, the demo plugin's registrar is
# baked into `sturm-transpile-plugin.so` at link-time. A `PLUGINS
# $<TARGET_FILE:sturm-pm4-demo-plugin>` argument then requests a second
# dlopen of the same plugin at runtime. The post-PM4-3
# `TranspileConsumer` ctor drains BOTH `runtime_registrars()` (from the
# dlopen) and `registrars()` (from the link-time bake), which triggers
# the Registry's duplicate-kind_id abort for `pm4.demo.tag`. That is the
# correct Registry behaviour, but it means the fixture compile aborts —
# so the pm4_smoke_dlopen test cannot exercise a clean end-to-end
# PLUGINS flow under this build config. The pm4_smoke_linktime smoke
# (PM4-10) covers the link-time path separately. Skip this smoke when
# STURM_PM4_LINK_DEMO=ON and log why at configure time so a reviewer
# does not chase a phantom regression.
if(STURM_TRANSPILE_MODE STREQUAL "plugin" AND STURM_PLUGIN_CLANGXX_BIN
        AND NOT STURM_PM4_LINK_DEMO)
    add_quantum_executable(pm4_smoke_dlopen_fixture
        fixtures/pm4_smoke_dlopen_fixture.cpp
        PLUGINS $<TARGET_FILE:sturm-pm4-demo-plugin>)
    # `add_quantum_executable` only wires `add_dependencies(<target>
    # sturm-transpile-plugin)` for the host plugin. The DEMO plugin
    # path is threaded via `-Xclang ... load=$<TARGET_FILE:...>` but
    # the helper does NOT add a build-order dependency on it — that
    # would require the helper to know about every PLUGINS entry at
    # configure time, which it deliberately does not (PLUGINS paths
    # are raw strings, not necessarily CMake targets). Add the
    # explicit dependency here so the demo `.so` is built before the
    # fixture compile tries to `load=` it.
    add_dependencies(pm4_smoke_dlopen_fixture sturm-pm4-demo-plugin)

    # ── Test binary: `pm4_smoke_dlopen` ─────────────────────────────────
    # Standalone `int main()` harness (same posture as
    # `test_plugin_load`). Takes five compile-time macros describing
    # the on-disk artifacts the fixture target produced, so the
    # binary has zero configure-time knowledge of the build-tree
    # layout. The `$<TARGET_FILE:...>` / `$<TARGET_OBJECTS:...>`
    # generator expressions resolve at build time against the
    # fixture target declared above.
    #
    # `$<TARGET_OBJECTS:...>` would give us a ;-separated list of
    # every object the target produced; for a single-source target
    # like `pm4_smoke_dlopen_fixture` that is exactly one `.o`. We
    # pass the path as a double-quoted string so the macro expands
    # into a C string literal inside the test TU.
    add_executable(pm4_smoke_dlopen
        pm4_smoke_dlopen.cpp
    )
    target_compile_features(pm4_smoke_dlopen PRIVATE cxx_std_20)
    target_compile_definitions(pm4_smoke_dlopen PRIVATE
        STURM_PLUGIN_PATH="$<TARGET_FILE:sturm-transpile-plugin>"
        STURM_PLUGIN_CLANGXX="${STURM_PLUGIN_CLANGXX_BIN}"
        STURM_PM4_DEMO_PLUGIN_PATH="$<TARGET_FILE:sturm-pm4-demo-plugin>"
        # The fixture target's final executable path. `$<TARGET_FILE:...>`
        # resolves to the linked artifact (no extension on Linux,
        # `.exe` on Windows), produced by the nested EmitObjAction
        # driving a clang++ link step. Its existence on disk is our
        # "the PLUGINS-driven compile + nested codegen + link all
        # completed" gate — a silent plugin load failure earlier in
        # the pipeline would have left this file absent.
        STURM_SMOKE_DLOPEN_FIXTURE_OBJ="$<TARGET_FILE:pm4_smoke_dlopen_fixture>"
        # The dump-to mirror path. `add_quantum_executable`'s plugin
        # mode computes `${CMAKE_BINARY_DIR}/sturm_gen/<relpath>` per
        # source (see `cmake/SturmTranspile.cmake:297-301`). The
        # relative path this resolves to is:
        #   transpiler/tests/fixtures/pm4_smoke_dlopen_fixture.cpp
        # because the fixture source lives under
        # `${CMAKE_SOURCE_DIR}/transpiler/tests/fixtures/`.
        STURM_SMOKE_DLOPEN_DUMP_PATH="${CMAKE_BINARY_DIR}/sturm_gen/transpiler/tests/fixtures/pm4_smoke_dlopen_fixture.cpp"
    )
    # Build-order dependencies: the test binary reads artifacts the
    # fixture target + the host plugin + the demo plugin produce, so
    # all three must exist on disk before the test binary's `main()`
    # runs. `add_quantum_executable` already set up the
    # `pm4_smoke_dlopen_fixture → sturm-transpile-plugin` edge; we
    # add the `pm4_smoke_dlopen → pm4_smoke_dlopen_fixture` edge +
    # the `pm4_smoke_dlopen → sturm-pm4-demo-plugin` edge (needed for
    # `gate_verbose_load_observes_kind_id`'s replay compile, which
    # dlopens the demo .so against a scratch TU).
    add_dependencies(pm4_smoke_dlopen
        pm4_smoke_dlopen_fixture
        sturm-transpile-plugin
        sturm-pm4-demo-plugin)

    # Wrap the ctest invocation in `pm4_smoke_dlopen.cmake` so the
    # test script mirrors the `.cmake` shape used by
    # `pm4_smoke_linktime` — outer harness runs the binary, surfaces
    # a clear failure message if the exit is non-zero, documents the
    # contract this smoke pins (see the .cmake file's comment block).
    add_test(NAME pm4_smoke_dlopen
        COMMAND ${CMAKE_COMMAND}
            -DTEST_BIN=$<TARGET_FILE:pm4_smoke_dlopen>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/pm4_smoke_dlopen.cmake
    )
    set_tests_properties(pm4_smoke_dlopen PROPERTIES LABELS "transpiler")
else()
    if(NOT STURM_TRANSPILE_MODE STREQUAL "plugin")
        message(STATUS "sturm-transpile: pm4_smoke_dlopen skipped — "
                       "STURM_TRANSPILE_MODE is '${STURM_TRANSPILE_MODE}', "
                       "not 'plugin'. PM4-8 exercises the PLUGINS cc1 "
                       "arg threading which only applies to plugin mode.")
    elseif(STURM_PM4_LINK_DEMO)
        message(STATUS "sturm-transpile: pm4_smoke_dlopen skipped — "
                       "STURM_PM4_LINK_DEMO=ON bakes the demo plugin "
                       "into sturm-transpile-plugin.so at link-time, "
                       "so a second runtime-dlopen of the same plugin "
                       "via PLUGINS=$<TARGET_FILE:sturm-pm4-demo-plugin> "
                       "would hit the post-PM4-3 Registry drain's "
                       "duplicate-kind_id abort. pm4_smoke_linktime "
                       "(PM4-10) covers the link-time path instead.")
    else()
        message(STATUS "sturm-transpile: pm4_smoke_dlopen skipped — "
                       "clang++ not found next to "
                       "LLVM_TOOLS_BINARY_DIR (STURM_PLUGIN_CLANGXX_BIN "
                       "unset). The test's verbose-replay leg needs a "
                       "reachable clang++ driver.")
    endif()
endif()

# ── PM4-9 smoke 3: pm4_dogfood_snapshot (byte-identity post-migration) ─────
# Runs the standalone `sturm-transpile` binary against the four Phase B
# compound-assign fixtures under `tests/transpiler/fixtures/` (add / sub /
# mul / div `_assign_const`) and byte-compares each generated sibling
# against its locked-down `.expected.cpp` golden. Mirrors the in-tree
# `snapshot_{add,sub,mul,div}_assign_const` CTest gates but is co-located
# with the other `pm4_smoke_*` binaries so `ctest -R pm4_` surfaces the
# full PM4 smoke set in one group. The contract this smoke pins: the
# PM4-6 dogfood migration of the four PB matchers to the Registry API
# (`STURM_REGISTER_PLUGIN(PBDogfoodPlugin)` at `matcher_qint_const.cpp:
# 280`) is transparent to the rewritten-output surface.
#
# No dependency on a plugin `.so`: the standalone driver already embeds
# every in-tree matcher via its object-file link line, so the dogfood
# migration's link-time `StaticRegistrar` ctors fire during static-init
# of `sturm-transpile` itself. The PB matchers are reachable through the
# consumer ctor's drain of `registrars()` without any runtime dlopen.
add_executable(pm4_dogfood_snapshot
    pm4_dogfood_snapshot.cpp
)
target_compile_features(pm4_dogfood_snapshot PRIVATE cxx_std_20)
target_compile_definitions(pm4_dogfood_snapshot PRIVATE
    STURM_TRANSPILE_BIN="$<TARGET_FILE:sturm-transpile>"
    # The four PB fixtures (`add_assign_const.cpp` / `sub_assign_const.cpp`
    # / `mul_assign_const.cpp` / `div_assign_const.cpp`) + their
    # `.expected.cpp` goldens live under `tests/transpiler/fixtures/`.
    # Thread the absolute path in at configure time so the binary does
    # not have to guess at the source-tree layout — mirrors the
    # `STURM_TRANSPILE_FIXTURES_DIR` macro the skip test uses.
    STURM_PM4_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/transpiler/fixtures"
    # Scratch-output root: the test binary creates per-fixture
    # subdirectories under this path and the `sturm-transpile
    # --output-dir` argument writes the generated sibling into them.
    # Same shape as the `OUTPUT_DIR` parameter `run_snapshot.cmake`
    # passes through at line 40 of `tests/transpiler/CMakeLists.txt`.
    STURM_PM4_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/gen/pm4_dogfood_snapshot"
)
add_dependencies(pm4_dogfood_snapshot sturm-transpile)
add_test(NAME pm4_dogfood_snapshot
    COMMAND ${CMAKE_COMMAND}
        -DTEST_BIN=$<TARGET_FILE:pm4_dogfood_snapshot>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/pm4_dogfood_snapshot.cmake
)
set_tests_properties(pm4_dogfood_snapshot PROPERTIES LABELS "transpiler")

# ── PM4-9 smoke 4: pm4_missing_plugin_errors ───────────────────────────────
# Drives the `add_quantum_executable(... PLUGINS <path>)` cc1 arg pattern
# (`-Xclang -plugin-arg-sturm-transpile -Xclang load=<path>`) with a path
# that is guaranteed NOT to exist on any CI host, and asserts that the
# compile fails with the documented `sturm-transpile plugin: failed to
# dlopen` stderr prefix (`plugin.cpp:195-198`). Also pins that `verbose`
# mode does NOT convert the fatal dlopen failure into a silent pass.
#
# Gated on `STURM_PLUGIN_CLANGXX_BIN`: the test shells out to clang++ for
# its single probe, so without a reachable driver the smoke cannot run.
# Same gating posture as `test_plugin_load` / `pm4_smoke_dlopen`.
if(STURM_PLUGIN_CLANGXX_BIN)
    add_executable(pm4_missing_plugin_errors
        pm4_missing_plugin_errors.cpp
    )
    target_compile_features(pm4_missing_plugin_errors PRIVATE cxx_std_20)
    target_compile_definitions(pm4_missing_plugin_errors PRIVATE
        STURM_PLUGIN_PATH="$<TARGET_FILE:sturm-transpile-plugin>"
        STURM_PLUGIN_CLANGXX="${STURM_PLUGIN_CLANGXX_BIN}"
    )
    add_dependencies(pm4_missing_plugin_errors sturm-transpile-plugin)
    add_test(NAME pm4_missing_plugin_errors
        COMMAND ${CMAKE_COMMAND}
            -DTEST_BIN=$<TARGET_FILE:pm4_missing_plugin_errors>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/pm4_missing_plugin_errors.cmake
    )
    set_tests_properties(pm4_missing_plugin_errors PROPERTIES
        LABELS "transpiler")
else()
    message(STATUS "sturm-transpile: pm4_missing_plugin_errors skipped — "
                   "clang++ not found next to LLVM_TOOLS_BINARY_DIR "
                   "(STURM_PLUGIN_CLANGXX_BIN unset).")
endif()

# ── PM4-9 smoke 5: pm4_two_plugins_independent ─────────────────────────────
# Pins the Registry's duplicate-`kind_id` collision detection
# (`plugin_registry.cpp:108-113`) end-to-end across two independently
# built runtime-loadable MODULE plugins that both claim `pm4.demo.tag`.
#
# The "independent second plugin" is built here as
# `sturm-pm4-collision-plugin` from
# `transpiler/tests/fixtures/pm4_collision_plugin.cpp` — a minimal
# MODULE library whose `sturm_register_plugin_v1` entry calls
# `register_op("pm4.demo.tag", ...)` with a different renderer body
# (`pm4_collision_tag_inverse(` instead of `pm4_demo_tag_inverse(`) so
# the two plugins are fully independent on the source surface. The
# single-source-of-truth for the collision target string lives on the
# first demo plugin at `examples/plugin_demo/plugin_demo.cpp:89`.
#
# Build target (collision plugin MODULE library):
# - MODULE library (dlopen-only), same posture as the first demo plugin.
# - Export `PREFIX "lib"` so the filename is predictable
#   (`libsturm-pm4-collision-plugin.so` on Linux /`.dylib` on macOS).
# - On macOS, `-undefined dynamic_lookup` is needed so the plugin's
#   Registry method references resolve against the host at dlopen time.
#   Same shape as `examples/plugin_demo/CMakeLists.txt:132-139`.
if(STURM_PLUGIN_CLANGXX_BIN)
    add_library(sturm-pm4-collision-plugin MODULE
        fixtures/pm4_collision_plugin.cpp
    )
    target_include_directories(sturm-pm4-collision-plugin PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${LLVM_INCLUDE_DIRS}
        ${CLANG_INCLUDE_DIRS}
    )
    if(NOT LLVM_ENABLE_RTTI)
        target_compile_options(sturm-pm4-collision-plugin PRIVATE
            -fno-rtti)
    endif()
    target_compile_features(sturm-pm4-collision-plugin PRIVATE cxx_std_20)
    separate_arguments(LLVM_DEFS_LIST_PM4_COLLISION NATIVE_COMMAND
        "${LLVM_DEFINITIONS}")
    target_compile_definitions(sturm-pm4-collision-plugin PRIVATE
        ${LLVM_DEFS_LIST_PM4_COLLISION})
    set_target_properties(sturm-pm4-collision-plugin PROPERTIES
        PREFIX "lib"
    )
    if(APPLE)
        target_link_options(sturm-pm4-collision-plugin PRIVATE
            "LINKER:-undefined,dynamic_lookup")
    endif()

    add_executable(pm4_two_plugins_independent
        pm4_two_plugins_independent.cpp
    )
    target_compile_features(pm4_two_plugins_independent PRIVATE cxx_std_20)
    target_compile_definitions(pm4_two_plugins_independent PRIVATE
        STURM_PLUGIN_PATH="$<TARGET_FILE:sturm-transpile-plugin>"
        STURM_PLUGIN_CLANGXX="${STURM_PLUGIN_CLANGXX_BIN}"
        STURM_PM4_DEMO_PLUGIN_PATH="$<TARGET_FILE:sturm-pm4-demo-plugin>"
        STURM_PM4_COLLISION_PLUGIN_PATH="$<TARGET_FILE:sturm-pm4-collision-plugin>"
    )
    # Under `STURM_PM4_LINK_DEMO=ON`, the demo plugin's registrar is
    # baked into `sturm-transpile-plugin.so` at link time. The test's
    # single-plugin control experiment (gate C) would then itself
    # trigger the collision (first runtime `load=<demo>` + baked-in
    # link-time registration), so we defer gate C under that config —
    # gate B remains the authoritative collision assertion. Mirrors the
    # `STURM_PM4_LINK_DEMO_ENABLED` posture used by
    # `test_plugin_load.cpp:115-119` and `pm4_smoke_dlopen.cpp:322-326`.
    if(STURM_PM4_LINK_DEMO)
        target_compile_definitions(pm4_two_plugins_independent PRIVATE
            STURM_PM4_LINK_DEMO_ENABLED=1)
    endif()
    add_dependencies(pm4_two_plugins_independent
        sturm-transpile-plugin
        sturm-pm4-demo-plugin
        sturm-pm4-collision-plugin)
    add_test(NAME pm4_two_plugins_independent
        COMMAND ${CMAKE_COMMAND}
            -DTEST_BIN=$<TARGET_FILE:pm4_two_plugins_independent>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/pm4_two_plugins_independent.cmake
    )
    set_tests_properties(pm4_two_plugins_independent PROPERTIES
        LABELS "transpiler")
else()
    message(STATUS "sturm-transpile: pm4_two_plugins_independent skipped "
                   "— clang++ not found next to LLVM_TOOLS_BINARY_DIR "
                   "(STURM_PLUGIN_CLANGXX_BIN unset).")
endif()

# ── PM4-10 smoke: link-time registrar path (`STURM_PM4_LINK_DEMO=ON`) ───────
# Only registered when `-DSTURM_PM4_LINK_DEMO=ON` is on the configure
# command line (the matching `option()` lives in `transpiler/CMakeLists.txt`
# — see the file-level comment there for the build-posture rationale).
# When the option is OFF (the default), this CTest case is not declared
# and a full-suite `ctest` run neither builds nor exercises the
# link-time path. The PM4-10 workflow asks the user to reconfigure with
# `-DSTURM_PM4_LINK_DEMO=ON` and rerun CTest to enable the probe.
#
# The test binary links `plugin_demo.cpp` + `pm4_link_demo_registrar.cpp`
# directly — mirroring the PRODUCTION `sturm-transpile-plugin` link line
# under `STURM_PM4_LINK_DEMO=ON` (the same two TUs get added to the
# plugin library in that configuration). Linking the registrar shim
# into this test binary fires its namespace-scope `StaticRegistrar` ctor
# at process start; the test's `main` then drains the Meyer's-singleton
# vector and asserts the demo's `pm4.demo.tag` renderer is reachable and
# emits the expected uncompute text.
#
# Linkage mirrors `test_plugin_demo` (plugin_registry.cpp + qir.cpp for
# the Registry method bodies; shared libclang-cpp / libLLVM when
# available, fallback to the static per-component archives otherwise —
# needed because the demo TU's `#include "sturm/transpile/qir.hpp"`
# transitively pulls `clang/Basic/SourceLocation.h`, whose opaque ctor
# lives in libclangBasic).
if(STURM_PM4_LINK_DEMO)
    add_executable(pm4_smoke_linktime
        pm4_smoke_linktime.cpp
        ${CMAKE_SOURCE_DIR}/examples/plugin_demo/plugin_demo.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/pm4_link_demo_registrar.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/plugin_registry.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/qir.cpp
    )
    target_include_directories(pm4_smoke_linktime PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${LLVM_INCLUDE_DIRS}
        ${CLANG_INCLUDE_DIRS}
    )
    if(NOT LLVM_ENABLE_RTTI)
        target_compile_options(pm4_smoke_linktime PRIVATE -fno-rtti)
    endif()
    target_compile_features(pm4_smoke_linktime PRIVATE cxx_std_20)
    separate_arguments(LLVM_DEFS_LIST_PM4_SMOKE_LT NATIVE_COMMAND
        "${LLVM_DEFINITIONS}")
    target_compile_definitions(pm4_smoke_linktime PRIVATE
        ${LLVM_DEFS_LIST_PM4_SMOKE_LT})
    if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
        target_link_libraries(pm4_smoke_linktime PRIVATE
            ${STURM_LIBCLANG_CPP}
            ${STURM_LIBLLVM}
        )
    else()
        target_link_libraries(pm4_smoke_linktime PRIVATE
            clangBasic
        )
        llvm_map_components_to_libnames(LLVM_LIBS_PM4_SMOKE_LT support)
        target_link_libraries(pm4_smoke_linktime PRIVATE
            ${LLVM_LIBS_PM4_SMOKE_LT})
    endif()

    # Wrap the ctest invocation in `pm4_smoke_linktime.cmake` so the
    # test script is a self-contained driver (mirrors the `.cmake`
    # shape the PM4 plan specifies for every `pm4_smoke_*` smoke).
    # The `.cmake` script is an outer harness that: (1) runs the test
    # binary, (2) surfaces a clear failure message if the binary exits
    # non-zero, (3) documents the contract this smoke pins (same
    # rewritten-output assertion as smoke 1, but via the link-time
    # path — see the script's file-level comment).
    add_test(NAME pm4_smoke_linktime
        COMMAND ${CMAKE_COMMAND}
            -DTEST_BIN=$<TARGET_FILE:pm4_smoke_linktime>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/pm4_smoke_linktime.cmake
    )
    set_tests_properties(pm4_smoke_linktime PROPERTIES LABELS "transpiler")
endif()

# ── Integration test: driver ──────────────────────────────────────────────────
add_executable(test_transpile_driver
    test_driver.cpp
)
target_include_directories(test_transpile_driver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_compile_features(test_transpile_driver PRIVATE cxx_std_20)
# Pass the absolute path to the binary as a preprocessor macro so the test
# does not have to guess at the build-tree layout. Generator expression
# `$<TARGET_FILE:...>` resolves to the final executable path.
target_compile_definitions(test_transpile_driver PRIVATE
    STURM_TRANSPILE_BIN="$<TARGET_FILE:sturm-transpile>"
)
add_dependencies(test_transpile_driver sturm-transpile)
add_test(NAME test_transpile_driver COMMAND test_transpile_driver)
set_tests_properties(test_transpile_driver PROPERTIES LABELS "transpiler")
