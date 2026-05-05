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

# ── Unit test: render_qint_typename (sturm-65rs.7 / Beat C0) ────────────────
# The shared `render_qint_typename` helper is a header-only inline free
# function (transpiler/src/render_qint_typename.hpp) with no LibTooling or
# Clang dependency — just <sstream>/<string> — so the test compiles and
# links in under a second with no extra include paths.
add_executable(test_render_qint_typename
    test_render_qint_typename.cpp
)
target_include_directories(test_render_qint_typename PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_features(test_render_qint_typename PRIVATE cxx_std_20)
add_test(NAME test_render_qint_typename COMMAND test_render_qint_typename)
set_tests_properties(test_render_qint_typename PROPERTIES LABELS "transpiler")

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

# ── Unit test: matcher_reversible_validate (P-C / sturm-z2e8.5) ─────────────
# Phase P plan §2.1 P-C: pins the P9d validation pass that walks every
# `[[sturm::reversible]]` routine body and rejects measurement, classical
# I/O, unregistered callees, while-loops, and quantum-dependent classical
# conditions. The validator fires diagnostics through the Phase P-D
# `DiagContext` `report_reversible_*` family added by sturm-z2e8.4.
#
# The test compiles small C++ snippets via `runToolOnCodeWithArgs`,
# locates the named reversible forward, wraps a `DiagnosticsEngine` that
# is fronted by a counting consumer (mirrors `test_diag_context.cpp`)
# inside a `DiagContext`, and invokes `validate_reversible_body`. Each
# rejection reason is exercised with its own happy / negative pair.
#
# Linkage pulls in `matcher_reversible_validate.cpp` (the module under
# test) plus the two read-side dependencies it consults:
# `reversible_attribute.cpp` for `is_reversible`, `routine_registry.cpp`
# for the PI-1 hand-registration bridge, and `diag_context.cpp` for the
# `report_reversible_*` surface. Shared libclang-cpp + libLLVM when
# available, fallback to the per-component archives otherwise — same
# shape every other direct-ASTConsumer test in this file uses.
add_executable(test_matcher_reversible_validate
    test_matcher_reversible_validate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_reversible_validate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/reversible_attribute.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/routine_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/diag_context.cpp
)
target_include_directories(test_matcher_reversible_validate PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_matcher_reversible_validate PRIVATE -fno-rtti)
endif()
target_compile_features(test_matcher_reversible_validate PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_REV_VALIDATE NATIVE_COMMAND
    "${LLVM_DEFINITIONS}")
target_compile_definitions(test_matcher_reversible_validate PRIVATE
    ${LLVM_DEFS_LIST_REV_VALIDATE})
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_matcher_reversible_validate PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_matcher_reversible_validate PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_REV_VALIDATE support)
    target_link_libraries(test_matcher_reversible_validate PRIVATE
        ${LLVM_LIBS_REV_VALIDATE})
endif()
add_test(NAME test_matcher_reversible_validate
    COMMAND test_matcher_reversible_validate)
set_tests_properties(test_matcher_reversible_validate PROPERTIES
    LABELS "transpiler")

# ── Unit test: matcher_reversible_signature (Q-B / sturm-5kgu.3) ────────────
# Phase Q plan §2.2 Q-B: pins the parameter-list signature enforcement
# pass for `[[sturm::reversible]]` forwards. The matcher rejects three
# classes per PRD §5.3 / §9 P9b:
#   1. PointerParam         — `qbool*` / `qint*` pointer-to-quantum.
#   2. ValueParamMutated    — non-const by-value quantum parameter
#                             mutated in the body.
#   3. ConstRefParamMutated — `const`-qualified reference-to-quantum
#                             parameter mutated in the body.
# Each reject fires through the Phase P-D `DiagContext`
# `report_reversible_*_param*` family added by sturm-5kgu.3.
#
# The test compiles small C++ snippets via `runToolOnCodeWithArgs`,
# locates the named reversible forward, wraps a `DiagnosticsEngine`
# that is fronted by a counting consumer (mirrors
# `test_matcher_reversible_validate.cpp`), wraps it in a `DiagContext`,
# and invokes `validate_reversible_signature`. Each rejection reason
# is exercised with its own happy / negative pair.
#
# Linkage pulls in `matcher_reversible_signature.cpp` (the module
# under test) plus its read-side dependencies:
# `reversible_attribute.cpp` for `is_reversible`, and
# `diag_context.cpp` for the `report_reversible_*_param*` surface.
# Shared libclang-cpp + libLLVM when available, fallback to the
# per-component archives otherwise — same shape every other direct-
# ASTConsumer test in this file uses.
add_executable(test_matcher_reversible_signature
    test_matcher_reversible_signature.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_reversible_signature.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/reversible_attribute.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/diag_context.cpp
)
target_include_directories(test_matcher_reversible_signature PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_matcher_reversible_signature PRIVATE -fno-rtti)
endif()
target_compile_features(test_matcher_reversible_signature PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_REV_SIG NATIVE_COMMAND
    "${LLVM_DEFINITIONS}")
target_compile_definitions(test_matcher_reversible_signature PRIVATE
    ${LLVM_DEFS_LIST_REV_SIG})
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_matcher_reversible_signature PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_matcher_reversible_signature PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_REV_SIG support)
    target_link_libraries(test_matcher_reversible_signature PRIVATE
        ${LLVM_LIBS_REV_SIG})
endif()
add_test(NAME test_matcher_reversible_signature
    COMMAND test_matcher_reversible_signature)
set_tests_properties(test_matcher_reversible_signature PROPERTIES
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
    # sturm-v0ur (LO-2 wiring): `render_uncompute()` now lives in its
    # own TU; link it for the adjoint_emitter test too.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/uncompute_render.cpp
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

# ── Unit test: auto_register_emitter (R-B / sturm-88d7.3) ───────────────────
# Phase R plan §2.3 R-B: pins the TU-scope `STURM_REGISTER_ADJOINT(fn,
# __fn_adj);` line emitted after each synthesized adjoint so PI-1's
# matcher (which consumes hand-written `STURM_REGISTER_ADJOINT(fn,
# adj)` invocations) picks up the synthesised pair on the second
# transpile pass. The tests exercise the pure-string API via direct
# golden comparisons and the FD-based API via `runToolOnCodeWithArgs`
# snippets — same harness shape `test_adjoint_emitter` uses.
#
# Linkage pulls in `auto_register_emitter.cpp` (module under test) plus
# `reversible_attribute.cpp` (the `is_reversible` predicate R-B
# consults on every FD-based input). Shared libclang-cpp + libLLVM
# when available, fallback to per-component archives otherwise — same
# shape every other direct-ASTConsumer test in this file uses.
add_executable(test_auto_register_emitter
    test_auto_register_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/auto_register_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/reversible_attribute.cpp
)
target_include_directories(test_auto_register_emitter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_auto_register_emitter PRIVATE -fno-rtti)
endif()
target_compile_features(test_auto_register_emitter PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_AUTO_REG NATIVE_COMMAND
    "${LLVM_DEFINITIONS}")
target_compile_definitions(test_auto_register_emitter PRIVATE
    ${LLVM_DEFS_LIST_AUTO_REG})
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_auto_register_emitter PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_auto_register_emitter PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_AUTO_REG support)
    target_link_libraries(test_auto_register_emitter PRIVATE
        ${LLVM_LIBS_AUTO_REG})
endif()
add_test(NAME test_auto_register_emitter COMMAND test_auto_register_emitter)
set_tests_properties(test_auto_register_emitter PROPERTIES
    LABELS "transpiler")

# ── Unit test: matcher_reversible_drive (R-C / sturm-88d7.4) ────────────────
# Phase R plan §2.3 R-C: pins the top-level driver that orchestrates R-A
# (`emit_adjoint_for_decl`) + R-B (`emit_auto_registration_for_decl`) for
# each `[[sturm::reversible]]` forward that passes P-C validation and
# Q-B constness. P-C (sturm-z2e8.5) and Q-B (sturm-5kgu.3) are NOT yet
# landed; the driver accepts swappable validator hooks so today's tests
# can drive happy / rejection / no-op paths with stand-in lambdas. The
# tests compile small C++ snippets via `runToolOnCodeWithArgs`, locate
# the relevant `FunctionDecl`, seed a `SynthesisRegistry` +
# `RoutineRegistry` by hand, and invoke `drive_reversible`.
#
# Linkage pulls in `matcher_reversible_drive.cpp` (module under test)
# plus every emitter / registry / renderer the driver calls:
# `adjoint_emitter.cpp` for R-A, `auto_register_emitter.cpp` for R-B,
# `synthesis_registry.cpp` for the per-forward book-keeping,
# `uncompute_pass.cpp` for `render_uncompute` (R-A's per-op
# dispatch), `reversible_attribute.cpp` for the `is_reversible`
# predicate, `plugin_registry.cpp` for the `plugin::Registry`
# destructor symbol referenced via `plugin_api.hpp`, and `qir.cpp`
# for the `QValueRef` equality operator referenced transitively
# from `qir.hpp`. Shared libclang-cpp + libLLVM when available,
# fallback to per-component archives otherwise — same shape every
# other direct-ASTConsumer test in this file uses.
add_executable(test_matcher_reversible_drive
    test_matcher_reversible_drive.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_reversible_drive.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_reversible_validate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_reversible_signature.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/return_to_out_param.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/diag_context.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/routine_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/adjoint_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/auto_register_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/synthesis_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/uncompute_pass.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/uncompute_render.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/reversible_attribute.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/plugin_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qir.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/source_map.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/io.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/skip.cpp
)
target_include_directories(test_matcher_reversible_drive PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_matcher_reversible_drive PRIVATE -fno-rtti)
endif()
target_compile_features(test_matcher_reversible_drive PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_REV_DRIVE NATIVE_COMMAND
    "${LLVM_DEFINITIONS}")
target_compile_definitions(test_matcher_reversible_drive PRIVATE
    ${LLVM_DEFS_LIST_REV_DRIVE})
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_matcher_reversible_drive PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_matcher_reversible_drive PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_REV_DRIVE support)
    target_link_libraries(test_matcher_reversible_drive PRIVATE
        ${LLVM_LIBS_REV_DRIVE})
endif()
add_test(NAME test_matcher_reversible_drive
    COMMAND test_matcher_reversible_drive)
set_tests_properties(test_matcher_reversible_drive PROPERTIES
    LABELS "transpiler")

# ── Unit test: loop_reversal (S-A / sturm-ha2k.2) ───────────────────────────
# Phase S plan §2.4 S-A: pins the reversed-iteration adjoint loop-header
# synthesis for `[[sturm::reversible]]` routines. The tests compile small
# C++ snippets via `runToolOnCodeWithArgs`, locate the first ForStmt
# inside a named enclosing function, and invoke `reverse_for_header`;
# stride 1 / 2 / negative cases assert the exact emitted header text,
# and the zero-stride / non-canonical cases assert the matching
# `LoopRejectReason`.
#
# Linkage pulls in `loop_reversal.cpp` (module under test). The module
# only depends on Clang's public AST + Lexer API (no other STURM
# translation units are needed). Shared libclang-cpp + libLLVM when
# available, fallback to per-component archives otherwise — same shape
# every other direct-ASTConsumer test in this file uses.
add_executable(test_loop_reversal
    test_loop_reversal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/loop_reversal.cpp
)
target_include_directories(test_loop_reversal PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_loop_reversal PRIVATE -fno-rtti)
endif()
target_compile_features(test_loop_reversal PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_LOOP_REV NATIVE_COMMAND
    "${LLVM_DEFINITIONS}")
target_compile_definitions(test_loop_reversal PRIVATE
    ${LLVM_DEFS_LIST_LOOP_REV})
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_loop_reversal PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_loop_reversal PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_LOOP_REV support)
    target_link_libraries(test_loop_reversal PRIVATE
        ${LLVM_LIBS_LOOP_REV})
endif()
add_test(NAME test_loop_reversal COMMAND test_loop_reversal)
set_tests_properties(test_loop_reversal PROPERTIES
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
    # sturm-v0ur (LO-2 wiring): per-op renderer hoisted out of
    # `uncompute_pass.cpp` so that file stays under the plan §9
    # 600-LoC budget. The unit-test still consumes the renderer via
    # the `render_uncompute()` symbol (declared in the same header),
    # so we link the new TU here too.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/uncompute_render.cpp
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

# ── Unit test: LO-2a lossy-op matcher (sturm-9254) ──────────────────────────
# Unit test for `register_lossy_op_matcher`. Drives a real Clang tooling
# invocation (runToolOnCodeWithArgs) against a 5-operator stub qint_t and
# asserts the matcher fires on `*=`, `/=`, `%=`, `&=`, `|=` against qint_t
# LHS, rejects reversible compound-assigns (`+=`, `-=`, `^=`) and plain
# `=`, and rejects non-qint LHS types (builtin int, user class `NotQInt`).
#
# The matcher is a stand-alone module with no QUnit dependency — it writes
# `LossyOpHit` records into a caller-owned vector. Link only the matcher
# TU itself; no transitive matcher-common deps (the .hpp is header-only)
# and no qir.cpp (no QUnit mutation).
add_executable(test_matcher_lossy_op
    test_matcher_lossy_op.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_lossy_op.cpp
)
target_include_directories(test_matcher_lossy_op PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_matcher_lossy_op PRIVATE -fno-rtti)
endif()
target_compile_features(test_matcher_lossy_op PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_LOSSY_OP NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_matcher_lossy_op PRIVATE
    ${LLVM_DEFS_LIST_LOSSY_OP})
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_matcher_lossy_op PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_matcher_lossy_op PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_LOSSY_OP support)
    target_link_libraries(test_matcher_lossy_op PRIVATE
        ${LLVM_LIBS_LOSSY_OP})
endif()
add_test(NAME test_matcher_lossy_op
         COMMAND test_matcher_lossy_op)
set_tests_properties(test_matcher_lossy_op PROPERTIES
    LABELS "transpiler")

# ── Unit test: LO-2b lossy_rewrite_emitter (sturm-yxxa) ─────────────────────
# Forward-only emitter consuming `LossyOpHit` records produced by LO-2a.
# Mirrors the matcher_lossy_op linkage: pure-string AST-driven harness, no
# QUnit / Rewriter dependency. Links the matcher_lossy_op TU because the
# AST-driven half of the test runs the matcher to produce live hits.
add_executable(test_lossy_rewrite_emitter
    test_lossy_rewrite_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/lossy_rewrite_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_lossy_op.cpp
)
target_include_directories(test_lossy_rewrite_emitter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_lossy_rewrite_emitter PRIVATE -fno-rtti)
endif()
target_compile_features(test_lossy_rewrite_emitter PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_LOSSY_EMIT NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_lossy_rewrite_emitter PRIVATE
    ${LLVM_DEFS_LIST_LOSSY_EMIT})
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_lossy_rewrite_emitter PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_lossy_rewrite_emitter PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_LOSSY_EMIT support)
    target_link_libraries(test_lossy_rewrite_emitter PRIVATE
        ${LLVM_LIBS_LOSSY_EMIT})
endif()
add_test(NAME test_lossy_rewrite_emitter
         COMMAND test_lossy_rewrite_emitter)
set_tests_properties(test_lossy_rewrite_emitter PROPERTIES
    LABELS "transpiler")

# ── Unit test: LO-2c lossy_scope_exit_emitter (sturm-hbwr) ──────────────────
# Cleanup-only emitter. Pairs LO-2a `LossyOpHit` with LO-2b `LossyEmission`
# to produce the §2.3 scope-exit `swap + invert` cleanup in LIFO order per
# enclosing CompoundStmt. Linkage matches the LO-2b test: pure-string AST-
# driven harness, no QUnit / Rewriter dependency. Links the LO-2a matcher
# TU AND the LO-2b emitter TU because the AST-driven half of the test runs
# both to produce live hits + matching forward emissions.
add_executable(test_lossy_scope_exit_emitter
    test_lossy_scope_exit_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/lossy_scope_exit_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/lossy_rewrite_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_lossy_op.cpp
)
target_include_directories(test_lossy_scope_exit_emitter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_lossy_scope_exit_emitter PRIVATE -fno-rtti)
endif()
target_compile_features(test_lossy_scope_exit_emitter PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_LOSSY_SCOPE NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_lossy_scope_exit_emitter PRIVATE
    ${LLVM_DEFS_LIST_LOSSY_SCOPE})
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_lossy_scope_exit_emitter PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_lossy_scope_exit_emitter PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_LOSSY_SCOPE support)
    target_link_libraries(test_lossy_scope_exit_emitter PRIVATE
        ${LLVM_LIBS_LOSSY_SCOPE})
endif()
add_test(NAME test_lossy_scope_exit_emitter
         COMMAND test_lossy_scope_exit_emitter)
set_tests_properties(test_lossy_scope_exit_emitter PROPERTIES
    LABELS "transpiler")

# ── Unit test: LO-2e lossy_nested_rewrite (sturm-rry6) ──────────────────────
# Depth-first rewrite for nested lossy ops. Drives the AST matcher + the
# combined inner-then-outer forward / outer-then-inner cleanup emitter. Links
# the LO-2a/2b/2c TUs so the AST harness reuses LO-2a's matcher to verify
# disjointness and `emit_nested_lossy` reuses the LO-2b/2c text emitters.
add_executable(test_lossy_nested_rewrite
    test_lossy_nested_rewrite.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/lossy_nested_rewrite.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/lossy_scope_exit_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/lossy_rewrite_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_lossy_op.cpp
)
target_include_directories(test_lossy_nested_rewrite PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_lossy_nested_rewrite PRIVATE -fno-rtti)
endif()
target_compile_features(test_lossy_nested_rewrite PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_LOSSY_NESTED NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_lossy_nested_rewrite PRIVATE
    ${LLVM_DEFS_LIST_LOSSY_NESTED})
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_lossy_nested_rewrite PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_lossy_nested_rewrite PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_LOSSY_NESTED support)
    target_link_libraries(test_lossy_nested_rewrite PRIVATE
        ${LLVM_LIBS_LOSSY_NESTED})
endif()
add_test(NAME test_lossy_nested_rewrite
         COMMAND test_lossy_nested_rewrite)
set_tests_properties(test_lossy_nested_rewrite PROPERTIES
    LABELS "transpiler")

# ── Unit test: P5.1 matcher_modular_op (sturm-qzab.1) ──────────────────────
# Beat 5.1 AddMod arm. Stand-alone matcher TU — writes `ModularOpHit`
# records into a caller-owned vector. Mirrors `test_matcher_lossy_op`'s
# linkage; no QUnit / Rewriter dependency.
add_executable(test_matcher_modular_op
    test_matcher_modular_op.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_modular_op.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_modular_compound_collapse.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_modular_pow_mod.cpp
)
target_include_directories(test_matcher_modular_op PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_matcher_modular_op PRIVATE -fno-rtti)
endif()
target_compile_features(test_matcher_modular_op PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_MOD_OP NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_matcher_modular_op PRIVATE
    ${LLVM_DEFS_LIST_MOD_OP})
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_matcher_modular_op PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_matcher_modular_op PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_MOD_OP support)
    target_link_libraries(test_matcher_modular_op PRIVATE
        ${LLVM_LIBS_MOD_OP})
endif()
if(STURM_MODULAR_POW)
    # sturm-qzab.5 (P5 beat 5.5): under the top-level STURM_MODULAR_POW=ON
    # configure, the test executable must exercise the matcher's flag-on
    # PowMod arm. We do this in two steps:
    #   1. Re-compile JUST the matcher TU (transpiler/src/matcher_modular_op.cpp)
    #      with `-DSTURM_MODULAR_POW` so the `#ifdef STURM_MODULAR_POW`
    #      registration block in `register_modular_op_matcher` fires.
    #   2. Set a SEPARATE test-only macro `STURM_TEST_MODULAR_POW_ON` on
    #      the whole test target so the test TU's `#ifdef` gate around the
    #      flag-on test functions activates.
    # The tests use the test-only macro (not `STURM_MODULAR_POW` directly)
    # so the `cmake_modular_pow_flag` ctest's compile_commands.json scan —
    # which forbids `-DSTURM_MODULAR_POW` outside `transpiler/src/` — does
    # not false-positive on `transpiler/tests/test_matcher_modular_op.cpp`.
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_modular_op.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_modular_pow_mod.cpp
        TARGET_DIRECTORY test_matcher_modular_op
        PROPERTIES COMPILE_DEFINITIONS STURM_MODULAR_POW)
    target_compile_definitions(test_matcher_modular_op PRIVATE
        STURM_TEST_MODULAR_POW_ON)
endif()
add_test(NAME test_matcher_modular_op
         COMMAND test_matcher_modular_op)
set_tests_properties(test_matcher_modular_op PROPERTIES
    LABELS "transpiler;modular-arith")

# ── Unit test: P5.1 modular_rewrite_emitter (sturm-qzab.1) ─────────────────
# Beat 5.1 AddMod arm. Pure-string + AST-driven harness. Linkage matches
# the matcher test; links the matcher TU because the AST-driven half
# of the test runs the matcher to produce live hits.
add_executable(test_modular_rewrite_emitter
    test_modular_rewrite_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modular_rewrite_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_modular_op.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_modular_compound_collapse.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_modular_pow_mod.cpp
)
target_include_directories(test_modular_rewrite_emitter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_modular_rewrite_emitter PRIVATE -fno-rtti)
endif()
target_compile_features(test_modular_rewrite_emitter PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_MOD_EMIT NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_modular_rewrite_emitter PRIVATE
    ${LLVM_DEFS_LIST_MOD_EMIT})
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_modular_rewrite_emitter PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_modular_rewrite_emitter PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_MOD_EMIT support)
    target_link_libraries(test_modular_rewrite_emitter PRIVATE
        ${LLVM_LIBS_MOD_EMIT})
endif()
if(STURM_MODULAR_POW)
    # sturm-qzab.5 (P5 beat 5.5): mirror the matcher-test wiring so the
    # AST-driven PowMod emission path is exercised under the flag-on
    # configure. See the corresponding block above (test_matcher_modular_op)
    # for the rationale on the STURM_MODULAR_POW vs STURM_TEST_MODULAR_POW_ON
    # split.
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_modular_op.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_modular_pow_mod.cpp
        TARGET_DIRECTORY test_modular_rewrite_emitter
        PROPERTIES COMPILE_DEFINITIONS STURM_MODULAR_POW)
    target_compile_definitions(test_modular_rewrite_emitter PRIVATE
        STURM_TEST_MODULAR_POW_ON)
endif()
add_test(NAME test_modular_rewrite_emitter
         COMMAND test_modular_rewrite_emitter)
set_tests_properties(test_modular_rewrite_emitter PROPERTIES
    LABELS "transpiler;modular-arith")

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
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/uncompute_render.cpp
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
    # Phase S S-B (sturm-ha2k.3): outer-var guard routes reversible
    # routines' for-loop mutations to Phase S loop reversal.
    test_matcher_sb_loop_reversal_handoff.cpp
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
    # Phase S S-B (sturm-ha2k.3): matcher_outer_var_guard.cpp now calls
    # `is_reversible()` from reversible_attribute.cpp to gate the
    # loop-reversal handoff. Link the TU so the matcher unit-test binary
    # resolves the symbol. reversible_attribute.cpp is a pure-string
    # predicate whose only Clang deps (AnnotateAttr) are already on the
    # link line via clangAST / clangBasic.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/reversible_attribute.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/uncompute_render.cpp
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
    # E7 (sturm-va3z): WHEN free-variable mutation diagnostic. M1 read-set
    # extractor + M2 write-set checker + M3 matcher wiring. Linked here so
    # `test_transpile_consumer` resolves the new
    # `register_when_freevar_matcher` symbol the consumer ctor invokes.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/when_freevar_readset.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/when_freevar_check.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_when_freevar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_brace_wrap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_outer_var_guard.cpp
    # Phase S S-B (sturm-ha2k.3): matcher_outer_var_guard.cpp now calls
    # `is_reversible()` from reversible_attribute.cpp to gate the
    # loop-reversal handoff. Link the TU so the consumer test binary
    # resolves the symbol (clangAST / clangBasic already on the link
    # line cover AnnotateAttr).
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/reversible_attribute.cpp
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
    # Phase T T-1 (sturm-xrob.2): TranspileConsumer now calls
    # `register_reversible_drive_matcher` (transpile_consumer.cpp:76) and
    # `drive_reversible_forwards` (transpile_consumer.cpp:564). Link the
    # driver TU plus its transitive validator / emitter / registry deps
    # (same source list the `test_matcher_reversible_drive` target
    # carries): matcher_reversible_drive.cpp is the module under
    # exercise, matcher_reversible_validate.cpp (P-C) and
    # matcher_reversible_signature.cpp (Q-B) supply the validator
    # surfaces the driver consults, return_to_out_param.cpp (Q-A)
    # populates the signature-normalisation registry entry the driver
    # reads, synthesis_registry.cpp is the per-forward book-keeping
    # container, adjoint_emitter.cpp (R-A) renders the __fn_adj body,
    # and auto_register_emitter.cpp (R-B) renders the
    # STURM_REGISTER_ADJOINT line. reversible_attribute.cpp and
    # routine_registry.cpp are already on the link line above (Phase S
    # S-B and PI-1 respectively), so they do not need repeating.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_reversible_drive.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_reversible_validate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_reversible_signature.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/return_to_out_param.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/synthesis_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/adjoint_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/auto_register_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/uncompute_pass.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/uncompute_render.cpp
    # sturm-v0ur (LO-2 wiring): TranspileConsumer now registers the
    # lossy compound-assign matcher + emitters. Link the three TUs so
    # the LO-2a/2b/2c symbols resolve at link time (the consumer's
    # post-walk drains the LO hits vector via `emit_lossy_forward` /
    # `group_cleanups_by_block` and feeds `register_external_cleanup()`
    # into `synthesize()`).
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_lossy_op.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/lossy_rewrite_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/lossy_scope_exit_emitter.cpp
    # sturm-rry6 (LO-2e): TranspileConsumer now registers the
    # nested-lossy matcher and drains nested hits in
    # `HandleTranslationUnit`. Link the LO-2e TU so
    # `register_nested_lossy_matcher` and `emit_nested_lossy` resolve.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/lossy_nested_rewrite.cpp
    # sturm-qzab.1/.2/.3 (P5 beats 5.1/5.2/5.3): TranspileConsumer
    # registers `register_modular_op_matcher` and drains
    # `modular_hits_` via `emit_modular_forward`. Link both TUs so
    # the consumer test resolves the symbols at link time. The beat
    # 5.3 compound-collapse arm rides the same translation units —
    # no additional sources required.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_modular_op.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_modular_compound_collapse.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_modular_pow_mod.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modular_rewrite_emitter.cpp
    # sturm-mm04 (sturm-ddgo regression fix): TranspileConsumer registers
    # the three QRAM-via-array-subscript matchers (C1 bare-init,
    # H1 existing-target, H4 expression-position) at construction
    # (transpile_consumer.cpp:293-297) and drains their hit vectors
    # through `emit_qram_*_replacements` in `HandleTranslationUnit`
    # (transpile_consumer.cpp:1118-1123). Link the six matcher /
    # emitter TUs plus the shared B1 width-inference helper so the
    # consumer test binary resolves the QRAM symbols at link time.
    # Mirrors the parallel block in `transpiler/CMakeLists.txt` for
    # `sturm-transpile` and `sturm-transpile-plugin`.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qram_subscript.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qram_subscript_assign.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qram_subscript_expr.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qram_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qram_emitter_assign.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qram_emitter_expr.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/width_inference.cpp
    # sturm-65rs.10 (Beat C3): TranspileConsumer registers the qint
    # alias substitution matcher in the ctor body and drains its
    # `qint_alias_subst_matches_` vector via
    # `emit_qint_alias_subst_replacements` AFTER the QRAM emitters
    # populate the `QintAliasSubstClaimedDecls` overlap-guard set.
    # Link the matcher + emitter TUs so the consumer test binary
    # resolves the new symbols at link time.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qint_alias_subst.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qint_alias_carrier_walk.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qint_alias_subst_emitter.cpp
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
# sturm-eksf: gated behind STURM_FULL_TEST_SUITE.
if(STURM_PLUGIN_CLANGXX_BIN AND STURM_PLUGIN_OBJDUMP_BIN AND STURM_FULL_TEST_SUITE)
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
        STURM_PLUGIN_GENERATED_INCLUDE_DIR="${CMAKE_BINARY_DIR}/include"
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

# ── Unit test: matcher_qram_oos (sturm-u9ge.16 / Beat E1) ─────────────────────
# Plan §8 / E1; PRD §9 / M5. The matcher under test recognises the four
# out-of-scope-shape PRD §9 spellings and fires a distinct Error-severity
# diagnostic per shape. Linkage mirrors test_transpile_diag_context: the
# matcher TU is the only `transpiler/src/*` source compiled in (it depends
# only on Clang's MatchFinder + DiagnosticsEngine — no DiagContext, no
# routine registry, no QUnit). Fixture inputs are threaded via
# STURM_QRAM_OOS_FIXTURES_DIR so the four golden cases load from the source
# tree.
add_executable(test_matcher_qram_oos
    test_matcher_qram_oos.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qram_oos.cpp
)
target_include_directories(test_matcher_qram_oos PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_matcher_qram_oos PRIVATE -fno-rtti)
endif()
target_compile_features(test_matcher_qram_oos PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_QRAM_OOS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_matcher_qram_oos PRIVATE
    ${LLVM_DEFS_LIST_QRAM_OOS}
    STURM_QRAM_OOS_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_matcher_qram_oos PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_matcher_qram_oos PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_QRAM_OOS support)
    target_link_libraries(test_matcher_qram_oos PRIVATE
        ${LLVM_LIBS_QRAM_OOS})
endif()
add_test(NAME test_matcher_qram_oos COMMAND test_matcher_qram_oos)
set_tests_properties(test_matcher_qram_oos PROPERTIES
    LABELS "transpiler;qram")

# ── Unit test: qint_implicit_warn (sturm-u9ge.14 / Beat F1) ──────────────────
# Plan §9 / F1; PRD §10.1 / M7. The matcher under test fires a
# `Warning`-severity diagnostic per non-subscript-context UDC site for
# `frontend::qint -> integer`. The warning is suppressed by default
# (placeholder static bool gate; eventual `-Wsturm-qint-implicit-measure`
# flag). Linkage mirrors test_matcher_qram_oos: the production TU is the
# only `transpiler/src/*` source compiled in — it depends only on Clang's
# MatchFinder + DiagnosticsEngine.
add_executable(test_qint_implicit_warn
    test_qint_implicit_warn.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qint_implicit_warn.cpp
)
target_include_directories(test_qint_implicit_warn PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_qint_implicit_warn PRIVATE -fno-rtti)
endif()
target_compile_features(test_qint_implicit_warn PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_QINT_WARN NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_qint_implicit_warn PRIVATE
    ${LLVM_DEFS_LIST_QINT_WARN}
)
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_qint_implicit_warn PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_qint_implicit_warn PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_QINT_WARN support)
    target_link_libraries(test_qint_implicit_warn PRIVATE
        ${LLVM_LIBS_QINT_WARN})
endif()
add_test(NAME test_qint_implicit_warn COMMAND test_qint_implicit_warn)
set_tests_properties(test_qint_implicit_warn PROPERTIES
    LABELS "transpiler;qram")

# ── Unit test: width_inference (sturm-u9ge.11 / Beat B1) ──────────────────────
# Plan §5 / B1; PRD §6 + §11.3 (D0c). The module under test decides the
# backend `qint_t<W>` width for a frontend `qint` VarDecl via three
# rules in strict order (annotation reserved → RHS-driven → default 32).
# Linkage mirrors test_matcher_qram_oos: the production TU is the only
# `transpiler/src/*` source compiled in — it depends only on Clang's AST
# + DiagnosticsEngine, no DiagContext, no routine registry, no QUnit.
add_executable(test_width_inference
    test_width_inference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/width_inference.cpp
)
target_include_directories(test_width_inference PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_width_inference PRIVATE -fno-rtti)
endif()
target_compile_features(test_width_inference PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_WIDTH_INF NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_width_inference PRIVATE
    ${LLVM_DEFS_LIST_WIDTH_INF}
)
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_width_inference PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_width_inference PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_WIDTH_INF support)
    target_link_libraries(test_width_inference PRIVATE
        ${LLVM_LIBS_WIDTH_INF})
endif()
add_test(NAME test_width_inference COMMAND test_width_inference)
set_tests_properties(test_width_inference PROPERTIES
    LABELS "transpiler;qram")

# ── Unit test: matcher_qram_subscript (sturm-u9ge.12 / Beat C1) ───────────────
# Plan §6 / C1; PRD §7 / M3. The matcher under test recognises exactly the
# v1 shape `qint b = a[i];` for the three container shapes (std::array,
# C-array, pointer). Linkage mirrors test_width_inference: the production
# TUs (matcher_qram_subscript.cpp + width_inference.cpp for B1's
# `infer_width()` plumbing) are the only `transpiler/src/*` sources
# compiled in. Fixture inputs are threaded via
# STURM_QRAM_SUBSCRIPT_FIXTURES_DIR so the three positive golden cases
# load from the source tree.
add_executable(test_matcher_qram_subscript
    test_matcher_qram_subscript.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qram_subscript.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/width_inference.cpp
)
target_include_directories(test_matcher_qram_subscript PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_matcher_qram_subscript PRIVATE -fno-rtti)
endif()
target_compile_features(test_matcher_qram_subscript PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_QRAM_SUBSCRIPT NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_matcher_qram_subscript PRIVATE
    ${LLVM_DEFS_LIST_QRAM_SUBSCRIPT}
    STURM_QRAM_SUBSCRIPT_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_matcher_qram_subscript PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_matcher_qram_subscript PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_QRAM_SUBSCRIPT support)
    target_link_libraries(test_matcher_qram_subscript PRIVATE
        ${LLVM_LIBS_QRAM_SUBSCRIPT})
endif()
add_test(NAME test_matcher_qram_subscript
         COMMAND test_matcher_qram_subscript)
set_tests_properties(test_matcher_qram_subscript PROPERTIES
    LABELS "transpiler;qram")

# ── Unit test: matcher_qint_alias_subst (sturm-65rs.8 / Beat C1) ─────────────
# Plan §9 / C1; PRD §4.3 match anchors. The matcher under test recognises
# every site at which a `sturm::frontend::qint` is spelled in source —
# VarDecl, ParmVarDecl, FieldDecl, function return type, and
# CXXFunctionalCastExpr. Linkage mirrors test_matcher_qram_subscript:
# the only `transpiler/src/*` source compiled in is
# `matcher_qint_alias_subst.cpp` (the matcher has no width-inference
# dependency at this beat — width is the C2 emitter's concern).
add_executable(test_matcher_qint_alias_subst
    test_matcher_qint_alias_subst.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qint_alias_subst.cpp
    # sturm-7t85.1 (Beat G1): carrier-walk sibling TU. Required so the
    # matcher's `typeloc_range_of` resolves `declarator_typeloc_range` at
    # link time.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qint_alias_carrier_walk.cpp
)
target_include_directories(test_matcher_qint_alias_subst PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_matcher_qint_alias_subst PRIVATE -fno-rtti)
endif()
target_compile_features(test_matcher_qint_alias_subst PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_QINT_ALIAS_SUBST NATIVE_COMMAND
    "${LLVM_DEFINITIONS}")
target_compile_definitions(test_matcher_qint_alias_subst PRIVATE
    ${LLVM_DEFS_LIST_QINT_ALIAS_SUBST})
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_matcher_qint_alias_subst PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_matcher_qint_alias_subst PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_QINT_ALIAS_SUBST support)
    target_link_libraries(test_matcher_qint_alias_subst PRIVATE
        ${LLVM_LIBS_QINT_ALIAS_SUBST})
endif()
add_test(NAME test_matcher_qint_alias_subst
         COMMAND test_matcher_qint_alias_subst)
set_tests_properties(test_matcher_qint_alias_subst PROPERTIES
    LABELS "transpiler;qac")

# ── Unit test: qint_alias_subst_emitter (sturm-65rs.9 / Beat C2) ────────────
# Plan §10 / C2; PRD §4.3 rewrite shape. The emitter under test consumes
# `QintAliasSubstMatch`es from C1 and rewrites every
# `sturm::frontend::qint` type spelling to `sturm::qint_t<W>`. Width
# routes through the existing `infer_width(VarDecl, InferContext)` for
# VarDecls; ParmVarDecl / FieldDecl / FunctionDecl-return /
# FunctionalCast all use `kDefaultWidth = 32` per PRD §3 non-goal.
#
# Linkage mirrors test_qram_emitter: the production TUs
# (qint_alias_subst_emitter.cpp + matcher_qint_alias_subst.cpp +
# width_inference.cpp) are the only `transpiler/src/*` sources compiled
# in. Fixture inputs / expected files live at transpiler/tests/fixtures/
# threaded via STURM_QINT_ALIAS_SUBST_EMITTER_FIXTURES_DIR.
add_executable(test_qint_alias_subst_emitter
    test_qint_alias_subst_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qint_alias_subst_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qint_alias_subst.cpp
    # sturm-7t85.1 (Beat G1): carrier-walk sibling TU.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qint_alias_carrier_walk.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/width_inference.cpp
)
target_include_directories(test_qint_alias_subst_emitter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_qint_alias_subst_emitter PRIVATE -fno-rtti)
endif()
target_compile_features(test_qint_alias_subst_emitter PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_QINT_ALIAS_SUBST_EM NATIVE_COMMAND
    "${LLVM_DEFINITIONS}")
target_compile_definitions(test_qint_alias_subst_emitter PRIVATE
    ${LLVM_DEFS_LIST_QINT_ALIAS_SUBST_EM}
    STURM_QINT_ALIAS_SUBST_EMITTER_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_qint_alias_subst_emitter PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_qint_alias_subst_emitter PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_QINT_ALIAS_SUBST_EM support)
    target_link_libraries(test_qint_alias_subst_emitter PRIVATE
        ${LLVM_LIBS_QINT_ALIAS_SUBST_EM})
endif()
add_test(NAME test_qint_alias_subst_emitter
         COMMAND test_qint_alias_subst_emitter)
set_tests_properties(test_qint_alias_subst_emitter PROPERTIES
    LABELS "transpiler;qac")

# ── Unit test: consumer_qint_alias_subst (sturm-65rs.10 / Beat C3) ──────────
# Plan §11 / C3; PRD §4.3 co-existence with the QRAM-subscript C1
# matcher / R2 single-VarDecl-single-rewrite invariant. Pins the C3
# consumer's claimed-decls overlap guard via a fixture containing both
# `qint b = a[i];` (QRAM C1) and `sturm::frontend::qint x;`
# (alias-subst). After the consumer drains both pipelines, `b` is
# rewritten ONLY by the QRAM emitter and `x` is rewritten ONLY by the
# alias-subst emitter — neither double-rewritten.
#
# Linkage compiles in the production TUs both pipelines need:
# qint_alias_subst_emitter.cpp + matcher_qint_alias_subst.cpp +
# qram_emitter.cpp + matcher_qram_subscript.cpp + width_inference.cpp +
# reversible_attribute.cpp. Fixture inputs/expected files live at
# transpiler/tests/fixtures/ threaded via
# STURM_CONSUMER_QINT_ALIAS_SUBST_FIXTURES_DIR.
add_executable(test_consumer_qint_alias_subst
    test_consumer_qint_alias_subst.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qint_alias_subst_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qint_alias_subst.cpp
    # sturm-7t85.1 (Beat G1): carrier-walk sibling TU.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qint_alias_carrier_walk.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qram_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qram_subscript.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/width_inference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/reversible_attribute.cpp
)
target_include_directories(test_consumer_qint_alias_subst PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_consumer_qint_alias_subst PRIVATE -fno-rtti)
endif()
target_compile_features(test_consumer_qint_alias_subst PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_CONSUMER_QAS NATIVE_COMMAND
    "${LLVM_DEFINITIONS}")
target_compile_definitions(test_consumer_qint_alias_subst PRIVATE
    ${LLVM_DEFS_LIST_CONSUMER_QAS}
    STURM_CONSUMER_QINT_ALIAS_SUBST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_consumer_qint_alias_subst PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_consumer_qint_alias_subst PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_CONSUMER_QAS support)
    target_link_libraries(test_consumer_qint_alias_subst PRIVATE
        ${LLVM_LIBS_CONSUMER_QAS})
endif()
add_test(NAME test_consumer_qint_alias_subst
         COMMAND test_consumer_qint_alias_subst)
set_tests_properties(test_consumer_qint_alias_subst PROPERTIES
    LABELS "transpiler;qac")

# ── Unit test: qram_emitter (sturm-u9ge.15 / Beat D2) ────────────────────────
# Plan §7 / D2; PRD §8 / M4. The emitter under test consumes
# `QramSubscriptHit`s from C1 and produces a two-line `qint_t<W> b;
# QRAM_read(a, i, b);` rewrite via Clang's Rewriter, with adjoint
# placement at the enclosing reversible scope's close brace per D0d.5.
# Linkage mirrors test_matcher_qram_subscript: the production TUs
# (qram_emitter.cpp + matcher_qram_subscript.cpp + width_inference.cpp
# + reversible_attribute.cpp) are the only `transpiler/src/*` sources
# compiled in. Fixture inputs / expected files live at
# transpiler/tests/fixtures/ threaded via STURM_QRAM_EMITTER_FIXTURES_DIR.
add_executable(test_qram_emitter
    test_qram_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qram_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qram_subscript.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/width_inference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/reversible_attribute.cpp
)
target_include_directories(test_qram_emitter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_qram_emitter PRIVATE -fno-rtti)
endif()
target_compile_features(test_qram_emitter PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_QRAM_EMITTER NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_qram_emitter PRIVATE
    ${LLVM_DEFS_LIST_QRAM_EMITTER}
    STURM_QRAM_EMITTER_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_qram_emitter PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_qram_emitter PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_QRAM_EMITTER support)
    target_link_libraries(test_qram_emitter PRIVATE
        ${LLVM_LIBS_QRAM_EMITTER})
endif()
add_test(NAME test_qram_emitter
         COMMAND test_qram_emitter)
set_tests_properties(test_qram_emitter PROPERTIES
    LABELS "transpiler;qram")

# ── Unit test: matcher_qram_subscript_expr (sturm-u9ge.9 / Beat H4) ──────────
# Plan §11 (post-v1 backlog) / Beat H4; PRD §9 row 4. The matcher under
# test recognises the expression-position QRAM-read shape
# `qint c = a[i] + d;` (and other binary-op shapes that wrap `a[i]`)
# at VarDecl-init position. Linkage mirrors test_matcher_qram_subscript:
# the production TUs (matcher_qram_subscript_expr.cpp + width_inference.cpp
# for B1's infer_width call) are the only `transpiler/src/*` sources
# compiled in.
add_executable(test_matcher_qram_subscript_expr
    test_matcher_qram_subscript_expr.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qram_subscript_expr.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/width_inference.cpp
)
target_include_directories(test_matcher_qram_subscript_expr PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_matcher_qram_subscript_expr PRIVATE -fno-rtti)
endif()
target_compile_features(test_matcher_qram_subscript_expr PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_QRAM_SUB_EXPR NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_matcher_qram_subscript_expr PRIVATE
    ${LLVM_DEFS_LIST_QRAM_SUB_EXPR}
)
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_matcher_qram_subscript_expr PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_matcher_qram_subscript_expr PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_QRAM_SUB_EXPR support)
    target_link_libraries(test_matcher_qram_subscript_expr PRIVATE
        ${LLVM_LIBS_QRAM_SUB_EXPR})
endif()
add_test(NAME test_matcher_qram_subscript_expr
         COMMAND test_matcher_qram_subscript_expr)
set_tests_properties(test_matcher_qram_subscript_expr PROPERTIES
    LABELS "transpiler;qram;qram-subscript")

# ── Unit test: qram_emitter_expr (sturm-u9ge.9 / Beat H4) ────────────────────
# Plan §11 (post-v1 backlog) / Beat H4; PRD §9 row 4. The emitter under
# test consumes `QramSubscriptExprHit`s from the H4 matcher and produces
# a three-part rewrite: pre-call ancilla extract + in-place subscript
# replacement + post-call uncompute. Linkage mirrors test_qram_emitter
# (qram_emitter_expr.cpp + matcher_qram_subscript_expr.cpp +
# width_inference.cpp are the only `transpiler/src/*` sources compiled
# in).
add_executable(test_qram_emitter_expr
    test_qram_emitter_expr.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qram_emitter_expr.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qram_subscript_expr.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/width_inference.cpp
)
target_include_directories(test_qram_emitter_expr PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_qram_emitter_expr PRIVATE -fno-rtti)
endif()
target_compile_features(test_qram_emitter_expr PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_QRAM_EMIT_EXPR NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_qram_emitter_expr PRIVATE
    ${LLVM_DEFS_LIST_QRAM_EMIT_EXPR}
)
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_qram_emitter_expr PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_qram_emitter_expr PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_QRAM_EMIT_EXPR support)
    target_link_libraries(test_qram_emitter_expr PRIVATE
        ${LLVM_LIBS_QRAM_EMIT_EXPR})
endif()
add_test(NAME test_qram_emitter_expr COMMAND test_qram_emitter_expr)
set_tests_properties(test_qram_emitter_expr PROPERTIES
    LABELS "transpiler;qram;qram-subscript")

# ── Unit test: matcher_qram_subscript_assign (sturm-u9ge.6 / Beat H1) ────────
# Plan §11 (post-v1 backlog) / Beat H1; PRD §9 row 1. The matcher under
# test recognises the existing-target QRAM-read shape `b = a[i];` where
# `b` is a pre-existing frontend `qint` variable. Linkage mirrors
# test_matcher_qram_subscript: the production TUs
# (matcher_qram_subscript_assign.cpp + width_inference.cpp for B1's
# infer_width / element-width helpers) are the only `transpiler/src/*`
# sources compiled in.
add_executable(test_matcher_qram_subscript_assign
    test_matcher_qram_subscript_assign.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qram_subscript_assign.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/width_inference.cpp
)
target_include_directories(test_matcher_qram_subscript_assign PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_matcher_qram_subscript_assign PRIVATE -fno-rtti)
endif()
target_compile_features(test_matcher_qram_subscript_assign PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_QRAM_SUB_ASSIGN NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_matcher_qram_subscript_assign PRIVATE
    ${LLVM_DEFS_LIST_QRAM_SUB_ASSIGN}
)
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_matcher_qram_subscript_assign PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_matcher_qram_subscript_assign PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_QRAM_SUB_ASSIGN support)
    target_link_libraries(test_matcher_qram_subscript_assign PRIVATE
        ${LLVM_LIBS_QRAM_SUB_ASSIGN})
endif()
add_test(NAME test_matcher_qram_subscript_assign
         COMMAND test_matcher_qram_subscript_assign)
set_tests_properties(test_matcher_qram_subscript_assign PROPERTIES
    LABELS "transpiler;qram;qram-subscript")

# ── Unit test: qram_emitter_assign (sturm-u9ge.6 / Beat H1) ──────────────────
# Plan §11 (post-v1 backlog) / Beat H1; PRD §9 row 1. The emitter under
# test consumes `QramSubscriptAssignHit`s from the H1 matcher and
# produces a two-statement rewrite per hit: pre-assign uncompute of
# old target + forward QRAM_read into target. Linkage mirrors
# test_qram_emitter_expr (qram_emitter_assign.cpp +
# matcher_qram_subscript_assign.cpp + width_inference.cpp are the
# only `transpiler/src/*` sources compiled in).
add_executable(test_qram_emitter_assign
    test_qram_emitter_assign.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qram_emitter_assign.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qram_subscript_assign.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/width_inference.cpp
)
target_include_directories(test_qram_emitter_assign PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_qram_emitter_assign PRIVATE -fno-rtti)
endif()
target_compile_features(test_qram_emitter_assign PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_QRAM_EMIT_ASSIGN NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_qram_emitter_assign PRIVATE
    ${LLVM_DEFS_LIST_QRAM_EMIT_ASSIGN}
)
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_qram_emitter_assign PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_qram_emitter_assign PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_QRAM_EMIT_ASSIGN support)
    target_link_libraries(test_qram_emitter_assign PRIVATE
        ${LLVM_LIBS_QRAM_EMIT_ASSIGN})
endif()
add_test(NAME test_qram_emitter_assign COMMAND test_qram_emitter_assign)
set_tests_properties(test_qram_emitter_assign PROPERTIES
    LABELS "transpiler;qram;qram-subscript")

# ── End-to-end gate: test_qram_e2e (sturm-u9ge.17 / Beat G1) ─────────────────
# Plan §10 / G1; PRD §5 (post-transpile safety net), §8 (M4 emission),
# §11.1 (D0a — three overloads), §11.2 (D0b — counter-mode/circuit-mode
# dispatch), §11.4 (D0d — adjoint registration). Closes the QRAM-via-
# array-subscript epic by exercising matcher (C1) + emitter (D2) +
# runtime stub (D1) across all three PRD §7 container shapes (StdArray
# / CArray / Pointer) under both counter-mode and circuit-mode sinks.
#
# Compiles in: the same transpiler/src/* TUs as test_qram_emitter (C1
# matcher + D2 emitter + B1 width inference + reversible-attr probe)
# AND the runtime body src/sturm/qram/qram_read.cpp so the in-process
# QRAM_read calls under both sinks resolve to the production helper
# pair. Mirrors the linkage shape of `tests/qram/test_qram_read_stub`.
#
# sturm-2w6h.4 (Beat B2): the test source defines `STURM_BACKEND_ENABLED 1`,
# so the public `QRAM_read` overloads now compile in the gate-emission
# path through `lib_qram_read_qrom_dsl`. The new template instantiation
# requires the M19 backend symbols (`sturm_get_thread_context`,
# `execute_gate`, control_stack, exec_simulate, orkan_bridge,
# uncompute_api). Linking those sources keeps the existing test passing
# under counter-mode and circuit-mode sinks; the umbrella `qram_read`
# counter contract from PRD §11.2.7 is preserved at the dispatcher.
enable_language(C)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_executable(test_qram_e2e
    test_qram_e2e.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qram_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qram_subscript.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/width_inference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/reversible_attribute.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/qram/qram_read.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
)
target_include_directories(test_qram_e2e PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_SOURCE_DIR}/include
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_qram_e2e PRIVATE -fno-rtti)
endif()
target_compile_features(test_qram_e2e PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_QRAM_E2E NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_qram_e2e PRIVATE
    ${LLVM_DEFS_LIST_QRAM_E2E}
)
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_qram_e2e PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_qram_e2e PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_QRAM_E2E support)
    target_link_libraries(test_qram_e2e PRIVATE
        ${LLVM_LIBS_QRAM_E2E})
endif()
target_link_libraries(test_qram_e2e PRIVATE orkan_headers)
add_test(NAME test_qram_e2e
         COMMAND test_qram_e2e)
set_tests_properties(test_qram_e2e PROPERTIES
    LABELS "transpiler;qram;qram-subscript")

# ── End-to-end gate vs. PRODUCTION headers: test_qram_e2e_real ────────────
# sturm-ddgo: companion to test_qram_e2e — the latter exercises the C1 /
# H1 / H4 + emitter pipelines against a hermetic stub; this binary
# transpiles the same shapes against the production headers
# (real `<array>` + real `qint_alias.hpp` + real `qint_core.hpp`).
# The gap between the stub's `template <int W>` qint_t and the
# production `template <std::size_t W>` qint_t — plus the extra
# `MaterializeTemporaryExpr` / `CXXBindTemporaryExpr` /
# `CXXConstructExpr` layers Clang emits when the production qint lacks
# a direct `qint& operator=(const qint_t<W>&)` overload — was the
# AST-shape gap sturm-ddgo closes. The `STURM_INCLUDE_DIR` /
# `STURM_VENDOR_DIR` defines steer LibTooling at the production
# headers in the same way `add_quantum_executable` does for the
# example targets.
add_executable(test_qram_e2e_real
    test_qram_e2e_real.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qram_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qram_emitter_assign.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qram_emitter_expr.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qram_subscript.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qram_subscript_assign.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qram_subscript_expr.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/width_inference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/reversible_attribute.cpp
)
target_include_directories(test_qram_e2e_real PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_SOURCE_DIR}/include
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_qram_e2e_real PRIVATE -fno-rtti)
endif()
target_compile_features(test_qram_e2e_real PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_QRAM_E2E_REAL NATIVE_COMMAND
    "${LLVM_DEFINITIONS}")
target_compile_definitions(test_qram_e2e_real PRIVATE
    ${LLVM_DEFS_LIST_QRAM_E2E_REAL}
    STURM_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/include"
    STURM_VENDOR_DIR="${CMAKE_SOURCE_DIR}/vendor"
)
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_qram_e2e_real PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_qram_e2e_real PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_QRAM_E2E_REAL support)
    target_link_libraries(test_qram_e2e_real PRIVATE
        ${LLVM_LIBS_QRAM_E2E_REAL})
endif()
# sturm-eksf: gated behind STURM_FULL_TEST_SUITE.
if(STURM_FULL_TEST_SUITE)
    add_test(NAME test_qram_e2e_real
             COMMAND test_qram_e2e_real)
    set_tests_properties(test_qram_e2e_real PROPERTIES
        LABELS "transpiler;qram;qram-subscript")
endif()

# ── End-to-end alias erasure: test_qint_alias_subst_e2e (sturm-65rs.13 / E1) ─
# Plan §14, PRD A5. Closes the qint-alias-completion epic by transpiling a
# fix-up'd copy of `examples/qram_demo.cpp`, asserting zero
# `sturm::frontend::qint` substrings post-transpile, and compile+running the
# generated file with all runtime sources `example_qram_demo` carries.
# The test invokes `sturm-transpile` and the host clang++ via popen — the
# CMake macros below thread the binaries / source paths so the test does
# not guess. Depends only on `sturm-transpile` so it runs even when
# `example_qram_demo` (the example target proper) is not buildable
# pre-F1 (sturm-65rs.14).
add_executable(test_qint_alias_subst_e2e
    test_qint_alias_subst_e2e.cpp
)
target_compile_features(test_qint_alias_subst_e2e PRIVATE cxx_std_20)
# Render the runtime-source list once so the test can splice it into a
# host-clang++ command line at run time. Mirrors the `target_sources`
# block on `example_qram_demo` in `examples/CMakeLists.txt:491-501`.
set(_e1_runtime_sources
    "${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c"
    "${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp"
    "${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp"
    "${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp"
    "${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp"
    "${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp"
    "${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp"
    "${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp"
    "${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp"
    "${CMAKE_SOURCE_DIR}/src/sturm/qram/qram_read.cpp"
)
string(JOIN " " _e1_runtime_sources_joined ${_e1_runtime_sources})
# Resolve the orkan-header include path the example links via
# `orkan_headers`. ORKAN_USE_STUB ⇒ vendor/orkan/; otherwise the
# fetched orkan_SOURCE_DIR. Use a single path on the command line so
# the test stays simple (no list quoting headaches in popen).
if(ORKAN_USE_STUB)
    set(_e1_orkan_include "${CMAKE_SOURCE_DIR}/vendor")
else()
    set(_e1_orkan_include "${orkan_SOURCE_DIR}/include")
endif()
target_compile_definitions(test_qint_alias_subst_e2e PRIVATE
    STURM_TRANSPILE_BIN="$<TARGET_FILE:sturm-transpile>"
    STURM_E1_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    STURM_E1_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/include"
    STURM_E1_CXX="${CMAKE_CXX_COMPILER}"
    STURM_E1_ANCILLA_CAPACITY=${STURM_ANCILLA_CAPACITY}
    STURM_E1_RUNTIME_SOURCES="${_e1_runtime_sources_joined}"
    STURM_E1_ORKAN_INCLUDE_DIR="${_e1_orkan_include}"
)
add_dependencies(test_qint_alias_subst_e2e sturm-transpile)
# sturm-eksf: gated behind STURM_FULL_TEST_SUITE.
if(STURM_FULL_TEST_SUITE)
    add_test(NAME test_qint_alias_subst_e2e
             COMMAND test_qint_alias_subst_e2e)
    set_tests_properties(test_qint_alias_subst_e2e PROPERTIES
        LABELS "transpiler;qac")
endif()
