# transpiler/tests/CMakeLists.txt — M4 test suite.
#
# Two test binaries:
#   test_io      — pure unit tests for sturm::transpile::{resolve_output_path,
#                  write_file, read_file}. No LibTooling, no subprocess.
#   test_driver  — end-to-end tests that invoke the compiled sturm-transpile
#                  binary. Uses STURM_TRANSPILE_BIN compile definition so the
#                  test does not hard-code a build-tree path.

cmake_minimum_required(VERSION 3.16)

# sturm-bs8s: pull in the `sturm_use_transpile_pch(<target>)` helper that
# opts a transpiler test executable into REUSE_FROM'ing the
# `sturm-transpile` PCH. The helper applies the `-fPIC` flag (required to
# match the producer's PIE state — sturm-2vbr) plus the
# `target_precompile_headers(... REUSE_FROM sturm-transpile)` call. Each
# test target that compiles heavy clang/llvm headers gets one
# `sturm_use_transpile_pch(<t>)` line below; targets that do NOT touch
# those headers (`test_transpile_io`, `test_transpile_fresh_names`,
# `test_render_qint_typename`, `test_transpile_skip`,
# `test_transpile_driver`, the subprocess-driven `pm4_*` smokes, the
# `test_qint_alias_subst_e2e` / `test_sturm_gen_clean*` driver tests)
# deliberately do NOT get the helper — adding the PCH to a sub-second
# TU costs more in PCH read time than parsing the original source
# saves.
include(${CMAKE_SOURCE_DIR}/cmake/SturmTranspilePch.cmake)

# ── sturm-hbwo: shared OBJECT library for transpiler/src TUs ─────────────────
# Bundles every `transpiler/src/*.cpp` that is safe to link into every
# test target (per the sturm-sfkt audit) into a single OBJECT library.
# Each consumer test executable replaces its per-target enumeration of
# `${CMAKE_CURRENT_SOURCE_DIR}/../src/*.cpp` files with
# `$<TARGET_OBJECTS:sturm_transpile_test_objs>`, eliminating the
# ~150 redundant compiles documented in sturm-sfkt and sturm-hbwo.
#
# Members (67 TUs)
# ----------------
# Every `transpiler/src/*.cpp` EXCEPT the four hold-outs listed below.
# See sturm-sfkt §5 for the full per-TU rationale.
#
# Hold-outs (NOT in this library)
# -------------------------------
#   - main.cpp                         — standalone-binary entry point;
#                                        registers cl::opt globals + a
#                                        second `int main`. Never linked
#                                        into a test.
#   - plugin.cpp                       — clang plugin entry; links
#                                        against the host's
#                                        FrontendPluginRegistry (sturm-yial
#                                        host-clang invariant).
#   - entry_point_parsed_attr_info.cpp — plugin-only
#                                        ParsedAttrInfoRegistry::Add<>
#                                        registrar (same invariant).
#   - pm4_link_demo_registrar.cpp      — option-gated link-time-path
#                                        probe; bundling defeats the
#                                        STURM_PM4_LINK_DEMO opt-in
#                                        semantics. Stays in
#                                        pm4_smoke_linktime's own list.
#
# One test target also stays on its custom source list:
#   - test_plugin_api_compile          — must NOT link
#                                        plugin_registry.cpp. Bundling
#                                        would silently rescue a future
#                                        broken inline in plugin_api.hpp.
#                                        See the dedicated comment +
#                                        `add_executable(test_plugin_api_
#                                        compile ...)` block lower in
#                                        this file.
#
# Compile configuration
# ---------------------
# Matches every consuming test target today: includes ../include,
# ../src, LLVM/Clang include dirs, c++20, ${LLVM_DEFINITIONS}, and
# -fno-rtti when LLVM_ENABLE_RTTI is off. PCH reuse via
# `sturm_use_transpile_pch` (mirroring sturm-bs8s' helper) applies
# `-fPIC` and `target_precompile_headers(... REUSE_FROM sturm-transpile)`.
#
# STURM_MODULAR_POW propagation
# -----------------------------
# Two TUs (`matcher_modular_op.cpp` + `matcher_modular_pow_mod.cpp`)
# carry `#ifdef STURM_MODULAR_POW` blocks. Today the define is added
# per-source via `set_source_files_properties` on the two test targets
# that need it. With the OBJECT library the source compile is owned by
# this target, so we apply the define at target scope when
# `STURM_MODULAR_POW=ON`. The 65 other TUs do not reference the macro;
# propagating it is observationally a no-op for them. The
# `cmake_modular_pow_flag` ctest buckets compile_commands entries by
# the source `file:` path (matches `/transpiler/src/`), so the
# OBJECT-library compiles register as driver hits, not stray hits.
add_library(sturm_transpile_test_objs OBJECT
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/adjoint_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/alias.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/auto_register_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/diag_context.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/entry_point_attribute.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/io.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/loop_reversal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/lossy_nested_rewrite.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/lossy_rewrite_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/lossy_scope_exit_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/main_lifecycle_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_brace_wrap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_ccnot_fuse.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_dead_ancilla.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_dropped_quantum_return.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_hoist_invariant.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_lossy_op.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_main_lifecycle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_modular_compound_collapse.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_modular_op.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_modular_pow_mod.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_outer_var_guard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_peephole_reorder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qbool_assign.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qbool_bitwise.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qbool_compound.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qbool_prep.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qint_alias_subst.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qint_compare.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qint_const.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qint_implicit_warn.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qint_qint.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qram_oos.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qram_subscript.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qram_subscript_assign.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_qram_subscript_expr.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_quantum_to_classical_cond.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_reversible_drive.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_reversible_signature.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_reversible_validate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_rotation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_user_routine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_when_freevar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_when_lift.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_when_nested.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_when_operand_mutation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modular_rewrite_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/plugin_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qint_alias_carrier_walk.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qint_alias_subst_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qir.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qram_emitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qram_emitter_assign.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qram_emitter_expr.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/return_to_out_param.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/reversible_attribute.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/routine_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/skip.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/source_map.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/synthesis_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/transpile_consumer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/uncompute_pass.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/uncompute_render.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/when_freevar_check.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/when_freevar_readset.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/width_inference.cpp
)
target_include_directories(sturm_transpile_test_objs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
target_compile_features(sturm_transpile_test_objs PUBLIC cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_OBJ NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(sturm_transpile_test_objs PUBLIC
    ${LLVM_DEFS_LIST_OBJ})
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(sturm_transpile_test_objs PRIVATE -fno-rtti)
endif()
if(STURM_MODULAR_POW)
    target_compile_definitions(sturm_transpile_test_objs PRIVATE
        STURM_MODULAR_POW)
endif()
sturm_use_transpile_pch(sturm_transpile_test_objs)

# Shared io helper sources, compiled into each test binary that needs them.
# Used by `test_transpile_io`, which deliberately does NOT link the
# OBJECT library (it has no LLVM/Clang dependency and stays minimal).
set(STURM_TRANSPILE_IO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/io.cpp
)

# ── sturm-r8xu: helper macro for bucket targets ───────────────────────────────
# Bundles ~10 per-family bucket binaries (`test_bucket_<family>`) that
# combine multiple logical ctest entries into a single link target.
# Eliminates 70+ s of duplicate `-j6` link work on the shared-libclang
# configure (sturm-vug6 measurement). Each member's `int main()` was
# renamed to `int run_<test_name>(int argc, char** argv)`; a per-bucket
# dispatcher (`bucket_dispatchers/test_bucket_<family>_main.cpp`)
# selects the right entry point based on `argv[1]`. ctest registration
# stays 1:1 — one `add_test(NAME <member> COMMAND $<TARGET_FILE:test_
# bucket_<family>> <member>)` per logical test — so failure isolation
# and `ctest --parallel 6` granularity are unchanged.
#
# This macro wires the common compile/link configuration every existing
# per-member test target carries today: include dirs, `cxx_std_20`,
# `LLVM_DEFINITIONS`, the `-fno-rtti` opt-in, the shared-vs-static
# libclang-cpp/libLLVM split (with the full LibTooling component
# fallback) and PCH reuse via `sturm_use_transpile_pch`. Per-bucket
# extras (extra source files, fixture-dir compile defs, extra link
# deps, PCH opt-out) are applied inline at each bucket's call site.
macro(sturm_define_bucket_target target)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${LLVM_INCLUDE_DIRS}
        ${CLANG_INCLUDE_DIRS}
    )
    if(NOT LLVM_ENABLE_RTTI)
        target_compile_options(${target} PRIVATE -fno-rtti)
    endif()
    target_compile_features(${target} PRIVATE cxx_std_20)
    separate_arguments(_sturm_bucket_defs NATIVE_COMMAND "${LLVM_DEFINITIONS}")
    target_compile_definitions(${target} PRIVATE ${_sturm_bucket_defs})
    if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
        target_link_libraries(${target} PRIVATE
            ${STURM_LIBCLANG_CPP}
            ${STURM_LIBLLVM}
        )
    else()
        target_link_libraries(${target} PRIVATE
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
        llvm_map_components_to_libnames(_sturm_bucket_llvm_libs support)
        target_link_libraries(${target} PRIVATE ${_sturm_bucket_llvm_libs})
    endif()
endmacro()

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
# sturm-bs8s: deliberately NO PCH here. test_transpile_qir compiles
# without any explicit `target_link_libraries` (qir.hpp only pulls
# clang/Basic/SourceLocation.h, whose ctor is inline-only). The PCH
# pulls in <clang/ASTMatchers/ASTMatchers.h>, which triggers
# template instantiations needing libclangASTMatchers / libclangAST
# symbols at link time — symbols this binary deliberately does not
# link. PCH read would force the cost of building + linking the full
# LibTooling stack onto a sub-second test. Skip.

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
# sturm-r8xu: bucketed into `test_bucket_pipeline` (defined below
# alongside the other pipeline-misc tests).

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
# sturm-r8xu: bucketed into `test_bucket_reversible` (defined further
# below alongside the other Phase P/Q/R/S reversibility tests).

# ── Unit test: matcher_main_lifecycle entry-point arm (sturm-0tcv) ──────────
# Exercises the extension of `matcher_main_lifecycle` that fires on any
# FunctionDecl carrying `[[sturm::entry_point]]`. Tests cover library
# fixtures, GoogleTest TEST_F wrapping idioms, the void/integral return
# split, macro gates, and the gtest-filename relaxation.
# ── sturm-r8xu: test_bucket_main_lifecycle ────────────────────────────────
# Bundles the sturm-0tcv main_lifecycle / entry_point arm tests:
#   test_matcher_main_lifecycle_entry_point  (matcher arm)
#   test_main_lifecycle_emitter_entry_point  (emitter arm)
# The emitter test reads on-disk fixtures via STURM_ENTRY_POINT_FIXTURES_DIR;
# the macro is applied at bucket-target scope (only test_main_lifecycle_
# emitter_entry_point.cpp references it; the other bucket member ignores it).
add_executable(test_bucket_main_lifecycle
    test_matcher_main_lifecycle_entry_point.cpp
    test_main_lifecycle_emitter_entry_point.cpp
    bucket_dispatchers/test_bucket_main_lifecycle_main.cpp
    $<TARGET_OBJECTS:sturm_transpile_test_objs>
)
sturm_define_bucket_target(test_bucket_main_lifecycle)
target_compile_definitions(test_bucket_main_lifecycle PRIVATE
    STURM_ENTRY_POINT_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
sturm_use_transpile_pch(test_bucket_main_lifecycle)
foreach(_t IN ITEMS
        test_matcher_main_lifecycle_entry_point
        test_main_lifecycle_emitter_entry_point)
    add_test(NAME ${_t}
        COMMAND $<TARGET_FILE:test_bucket_main_lifecycle> ${_t})
    set_tests_properties(${_t} PROPERTIES LABELS "transpiler")
endforeach()
unset(_t)

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
# ── sturm-r8xu: test_bucket_reversible ────────────────────────────────────
# Bundles the Phase P/Q/R/S reversibility / synthesis tests, plus the
# small attribute predicates (`reversible_attribute`,
# `entry_point_attribute`). All ten members consume the shared
# `sturm_transpile_test_objs` OBJECT library — their per-target source
# lists collapsed to the shared OBJECT library after sturm-hbwo
# (commit 66289cc), so bucketing them eliminates the per-test link
# cost without changing the compile-time surface.
#
#   test_matcher_reversible_drive    (R-C / sturm-88d7.4)
#   test_matcher_reversible_validate (P-C / sturm-z2e8.5)
#   test_matcher_reversible_signature(Q-B / sturm-5kgu.3)
#   test_synthesis_registry          (P-B / sturm-z2e8.3)
#   test_return_to_out_param         (Q-A / sturm-5kgu.2 — needs
#                                     STURM_RETURN_TO_OUT_PARAM_FIXTURES_DIR)
#   test_adjoint_emitter             (R-A / sturm-88d7.2)
#   test_auto_register_emitter       (R-B / sturm-88d7.3)
#   test_reversible_attribute        (P-A / sturm-z2e8.2)
#   test_loop_reversal               (S-A / sturm-ha2k.2)
#   test_entry_point_attribute       (sturm-0tcv)
add_executable(test_bucket_reversible
    test_matcher_reversible_drive.cpp
    test_matcher_reversible_validate.cpp
    test_matcher_reversible_signature.cpp
    test_synthesis_registry.cpp
    test_return_to_out_param.cpp
    test_adjoint_emitter.cpp
    test_auto_register_emitter.cpp
    test_reversible_attribute.cpp
    test_loop_reversal.cpp
    test_entry_point_attribute.cpp
    bucket_dispatchers/test_bucket_reversible_main.cpp
    $<TARGET_OBJECTS:sturm_transpile_test_objs>
)
sturm_define_bucket_target(test_bucket_reversible)
target_compile_definitions(test_bucket_reversible PRIVATE
    STURM_RETURN_TO_OUT_PARAM_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
sturm_use_transpile_pch(test_bucket_reversible)
foreach(_t IN ITEMS
        test_matcher_reversible_drive
        test_matcher_reversible_validate
        test_matcher_reversible_signature
        test_synthesis_registry
        test_return_to_out_param
        test_adjoint_emitter
        test_auto_register_emitter
        test_reversible_attribute
        test_loop_reversal
        test_entry_point_attribute)
    add_test(NAME ${_t}
        COMMAND $<TARGET_FILE:test_bucket_reversible> ${_t})
    set_tests_properties(${_t} PROPERTIES LABELS "transpiler")
endforeach()
unset(_t)

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
# ── sturm-r8xu: test_bucket_plugin ────────────────────────────────────────
# Bundles the in-process plugin Registry / demo / dogfood tests:
#   test_plugin_demo             (PM4-7 demo plugin contract)
#   test_plugin_registry         (PM4-2 Registry + StaticRegistrar)
#   test_pm4_dogfood_registry    (PM4-6 PB-1..PB-4 migration pin)
# The demo test also compiles in examples/plugin_demo/plugin_demo.cpp
# (the example plugin TU) so its `sturm_register_plugin_v1` entry point
# resolves at link time.
add_executable(test_bucket_plugin
    test_plugin_demo.cpp
    test_plugin_registry.cpp
    test_pm4_dogfood_registry.cpp
    ${CMAKE_SOURCE_DIR}/examples/plugin_demo/plugin_demo.cpp
    bucket_dispatchers/test_bucket_plugin_main.cpp
    $<TARGET_OBJECTS:sturm_transpile_test_objs>
)
sturm_define_bucket_target(test_bucket_plugin)
sturm_use_transpile_pch(test_bucket_plugin)
foreach(_t IN ITEMS
        test_plugin_demo
        test_plugin_registry
        test_pm4_dogfood_registry)
    add_test(NAME ${_t}
        COMMAND $<TARGET_FILE:test_bucket_plugin> ${_t})
    set_tests_properties(${_t} PROPERTIES LABELS "transpiler")
endforeach()
unset(_t)

# ── Unit test: M8 uncompute synthesis pass ──────────────────────────────────
# Like the M6 QIR test, M8 uses only clang::SourceLocation — an opaque 32-bit
# wrapper — so the test does NOT need to link any LibTooling components. It
# compiles in under a second and runs in microseconds because the QUnit under
# test is constructed by hand. This is the whole reason M8 is separate from
# M7 and M9: ordering regressions can be pinpointed without a compiler in the
# loop.
# sturm-r8xu: bucketed into `test_bucket_pipeline` (defined below).

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
# sturm-r8xu: bucketed into `test_bucket_pipeline` (defined below).

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
# sturm-r8xu: bucketed into `test_bucket_pipeline` (defined below).

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
# ── sturm-r8xu: test_bucket_lossy ──────────────────────────────────────────
# Bundles the LO-2 lossy-rewrite family unit tests:
#   test_matcher_lossy_op          (LO-2a / sturm-9254 matcher)
#   test_lossy_rewrite_emitter     (LO-2b / sturm-yxxa forward emitter)
#   test_lossy_scope_exit_emitter  (LO-2c / sturm-hbwr cleanup emitter)
#   test_lossy_nested_rewrite      (LO-2e / sturm-rry6 nested rewrite)
add_executable(test_bucket_lossy
    test_matcher_lossy_op.cpp
    test_lossy_rewrite_emitter.cpp
    test_lossy_scope_exit_emitter.cpp
    test_lossy_nested_rewrite.cpp
    bucket_dispatchers/test_bucket_lossy_main.cpp
    $<TARGET_OBJECTS:sturm_transpile_test_objs>
)
sturm_define_bucket_target(test_bucket_lossy)
sturm_use_transpile_pch(test_bucket_lossy)
foreach(_t IN ITEMS
        test_matcher_lossy_op
        test_lossy_rewrite_emitter
        test_lossy_scope_exit_emitter
        test_lossy_nested_rewrite)
    add_test(NAME ${_t}
        COMMAND $<TARGET_FILE:test_bucket_lossy> ${_t})
    set_tests_properties(${_t} PROPERTIES LABELS "transpiler")
endforeach()
unset(_t)

# ── sturm-r8xu: test_bucket_modular ────────────────────────────────────────
# Bundles the P5 modular-arith matcher + emitter unit tests:
#   test_matcher_modular_op       (sturm-qzab.1 Beat 5.1 AddMod matcher)
#   test_modular_rewrite_emitter  (sturm-qzab.1 Beat 5.1 AddMod emitter)
# Both files key the same `STURM_TEST_MODULAR_POW_ON` macro under
# `STURM_MODULAR_POW=ON`, so the define is added at bucket-target scope
# (mirrors the per-target wiring the original individual targets carried).
add_executable(test_bucket_modular
    test_matcher_modular_op.cpp
    test_modular_rewrite_emitter.cpp
    bucket_dispatchers/test_bucket_modular_main.cpp
    $<TARGET_OBJECTS:sturm_transpile_test_objs>
)
sturm_define_bucket_target(test_bucket_modular)
if(STURM_MODULAR_POW)
    target_compile_definitions(test_bucket_modular PRIVATE
        STURM_TEST_MODULAR_POW_ON)
endif()
sturm_use_transpile_pch(test_bucket_modular)
foreach(_t IN ITEMS test_matcher_modular_op test_modular_rewrite_emitter)
    add_test(NAME ${_t}
        COMMAND $<TARGET_FILE:test_bucket_modular> ${_t})
    set_tests_properties(${_t} PROPERTIES
        LABELS "transpiler;modular-arith")
endforeach()
unset(_t)

# ── sturm-r8xu: test_bucket_pipeline ──────────────────────────────────────
# Bundles the pipeline-misc unit tests:
#   test_transpile_emitter        (M9 C++ emitter)
#   test_transpile_uncompute_pass (M8 uncompute synthesis pass)
#   test_alias_footprint          (PM5-4 footprint + may_overlap)
#   test_width_inference          (sturm-u9ge.11 / Beat B1)
#   test_transpile_diag_context   (PM3-0 + P-D DiagContext)
#   test_qint_implicit_warn       (sturm-u9ge.14 / Beat F1)
#   test_matcher_peephole_reorder (PM5-7)
#
# The label set differs across members (most are `transpiler`; the
# B1/F1 pair carry the legacy `transpiler;qram` set the original
# individual targets registered under). Each `add_test` carries its
# own labels below.
add_executable(test_bucket_pipeline
    test_emitter.cpp
    test_uncompute_pass.cpp
    test_alias_footprint.cpp
    test_width_inference.cpp
    test_diag_context.cpp
    test_qint_implicit_warn.cpp
    test_matcher_peephole_reorder.cpp
    bucket_dispatchers/test_bucket_pipeline_main.cpp
    $<TARGET_OBJECTS:sturm_transpile_test_objs>
)
sturm_define_bucket_target(test_bucket_pipeline)
sturm_use_transpile_pch(test_bucket_pipeline)
foreach(_t IN ITEMS
        test_transpile_emitter
        test_transpile_uncompute_pass
        test_alias_footprint
        test_transpile_diag_context
        test_matcher_peephole_reorder)
    add_test(NAME ${_t}
        COMMAND $<TARGET_FILE:test_bucket_pipeline> ${_t})
    set_tests_properties(${_t} PROPERTIES LABELS "transpiler")
endforeach()
foreach(_t IN ITEMS
        test_width_inference
        test_qint_implicit_warn)
    add_test(NAME ${_t}
        COMMAND $<TARGET_FILE:test_bucket_pipeline> ${_t})
    set_tests_properties(${_t} PROPERTIES LABELS "transpiler;qram")
endforeach()
unset(_t)

# ── Unit test: M9 C++ emitter ────────────────────────────────────────────────
# The emitter test invokes a real Clang SourceManager + Rewriter so it
# mirrors the matcher test's linkage: shared libclang-cpp + libLLVM when
# available, fallback to the static per-component archives otherwise.
# sturm-r8xu: bucketed into `test_bucket_pipeline` (defined above).

# ── Unit test: M7 AST matcher ────────────────────────────────────────────────
# This test drives the `register_or_matcher` helper through a real Clang
# tooling invocation (runToolOnCodeWithArgs). It therefore needs to link
# against the same LibTooling components as sturm-transpile itself.
# sturm-r8xu: bucketed into `test_bucket_transpile_full` (defined below).

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
# ── sturm-r8xu: test_bucket_transpile_full ────────────────────────────────
# Bundles the two "mega" transpiler tests:
#   test_transpile_matcher   (M7 / sturm-65rs matcher pool — multi-source)
#   test_transpile_consumer  (PM1-1 TranspileConsumer EmissionMode boundary)
# The matcher test compiles 22+ sub-source files alongside its dispatcher
# (test_matcher.cpp); they all stay inside the bucket so the harness
# entry-point dispatch remains intact.
add_executable(test_bucket_transpile_full
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
    # PM1-1: TranspileConsumer EmissionMode boundary.
    test_transpile_consumer.cpp
    bucket_dispatchers/test_bucket_transpile_full_main.cpp
    $<TARGET_OBJECTS:sturm_transpile_test_objs>
)
sturm_define_bucket_target(test_bucket_transpile_full)
sturm_use_transpile_pch(test_bucket_transpile_full)
foreach(_t IN ITEMS test_transpile_matcher test_transpile_consumer)
    add_test(NAME ${_t}
        COMMAND $<TARGET_FILE:test_bucket_transpile_full> ${_t})
    set_tests_properties(${_t} PROPERTIES LABELS "transpiler")
endforeach()
unset(_t)

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
        # sturm-zva0: forward the `STURM_MODE_DEFAULT=STURM_MODE_<X>` compile
        # def carried by `sturm::frontend` (sturm-mixe / P1) as a `-D` flag
        # string. The auto-injected lifecycle prologue (sturm-e3ru / P7)
        # references `STURM_MODE_DEFAULT` in the rewritten source, so the
        # second-stage clang++ invocation (and the plugin nested action's
        # nested EmitObjAction) needs the macro defined. We pull the value
        # from the INTERFACE_COMPILE_DEFINITIONS of sturm_frontend so the
        # test stays in lockstep with the canonical source of truth.
        STURM_PLUGIN_MODE_DEFAULT_FLAG="-D$<JOIN:$<TARGET_PROPERTY:sturm_frontend,INTERFACE_COMPILE_DEFINITIONS>, -D>"
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
# sturm-r8xu: bucketed into `test_bucket_qram` (defined below).

# ── Unit test: qint_implicit_warn (sturm-u9ge.14 / Beat F1) ──────────────────
# Plan §9 / F1; PRD §10.1 / M7. The matcher under test fires a
# `Warning`-severity diagnostic per non-subscript-context UDC site for
# `frontend::qint -> integer`. The warning is suppressed by default
# (placeholder static bool gate; eventual `-Wsturm-qint-implicit-measure`
# flag). Linkage mirrors test_matcher_qram_oos: the production TU is the
# only `transpiler/src/*` source compiled in — it depends only on Clang's
# MatchFinder + DiagnosticsEngine.
# sturm-r8xu: test_qint_implicit_warn and test_width_inference bucketed
# into `test_bucket_pipeline` (defined below).

# ── Unit test: matcher_qram_subscript (sturm-u9ge.12 / Beat C1) ───────────────
# Plan §6 / C1; PRD §7 / M3. The matcher under test recognises exactly the
# v1 shape `qint b = a[i];` for the three container shapes (std::array,
# C-array, pointer). Linkage mirrors test_width_inference: the production
# TUs (matcher_qram_subscript.cpp + width_inference.cpp for B1's
# `infer_width()` plumbing) are the only `transpiler/src/*` sources
# compiled in. Fixture inputs are threaded via
# STURM_QRAM_SUBSCRIPT_FIXTURES_DIR so the three positive golden cases
# load from the source tree.
# sturm-r8xu: bucketed into `test_bucket_qram` (defined below).

# ── Unit test: matcher_qint_alias_subst (sturm-65rs.8 / Beat C1) ─────────────
# Plan §9 / C1; PRD §4.3 match anchors. The matcher under test recognises
# every site at which a `sturm::frontend::qint` is spelled in source —
# VarDecl, ParmVarDecl, FieldDecl, function return type, and
# CXXFunctionalCastExpr. Linkage mirrors test_matcher_qram_subscript:
# the only `transpiler/src/*` source compiled in is
# `matcher_qint_alias_subst.cpp` (the matcher has no width-inference
# dependency at this beat — width is the C2 emitter's concern).
# ── sturm-r8xu: test_bucket_qint_alias ────────────────────────────────────
# Bundles the qint-alias erasure C1 / C2 / C3 family:
#   test_matcher_qint_alias_subst   (sturm-65rs.8 / Beat C1)
#   test_qint_alias_subst_emitter   (sturm-65rs.9 / Beat C2 — fixture-driven)
#   test_consumer_qint_alias_subst  (sturm-65rs.10 / Beat C3 — fixture-driven)
# Fixture-dir defines for the two fixture-driven members are applied at
# bucket-target scope; the third member's TU ignores macros it does not
# `#ifdef`/`#define`-test on.
add_executable(test_bucket_qint_alias
    test_matcher_qint_alias_subst.cpp
    test_qint_alias_subst_emitter.cpp
    test_consumer_qint_alias_subst.cpp
    bucket_dispatchers/test_bucket_qint_alias_main.cpp
    $<TARGET_OBJECTS:sturm_transpile_test_objs>
)
sturm_define_bucket_target(test_bucket_qint_alias)
target_compile_definitions(test_bucket_qint_alias PRIVATE
    STURM_QINT_ALIAS_SUBST_EMITTER_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    STURM_CONSUMER_QINT_ALIAS_SUBST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
sturm_use_transpile_pch(test_bucket_qint_alias)
foreach(_t IN ITEMS
        test_matcher_qint_alias_subst
        test_qint_alias_subst_emitter
        test_consumer_qint_alias_subst)
    add_test(NAME ${_t}
        COMMAND $<TARGET_FILE:test_bucket_qint_alias> ${_t})
    set_tests_properties(${_t} PROPERTIES LABELS "transpiler;qac")
endforeach()
unset(_t)

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
# sturm-r8xu: bucketed into `test_bucket_qram` (defined below alongside
# the test_qram_e2e* end-to-end gates).

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
# ── sturm-r8xu: test_bucket_qram ──────────────────────────────────────────
# Bundles the entire QRAM-via-array-subscript epic (C1 / H1 / H4 matchers
# + emitters + oos + e2e). The bucket pulls in a small set of runtime
# sources so `test_qram_e2e`'s in-process counter-/circuit-sink dispatch
# resolves at link time; the legacy `gate_kind.c` C TU means PCH reuse
# is NOT applied to this bucket (mixed C/C++ targets cannot REUSE_FROM
# a C++-only PCH per sturm-bs8s).
#
# Members (logical ctest entries):
#   test_matcher_qram_subscript          (C1 matcher / fixture-driven)
#   test_matcher_qram_subscript_assign   (H1 matcher)
#   test_matcher_qram_subscript_expr     (H4 matcher)
#   test_qram_emitter                    (D2 emitter / fixture-driven)
#   test_qram_emitter_assign             (H1 emitter)
#   test_qram_emitter_expr               (H4 emitter)
#   test_matcher_qram_oos                (E1 OOS matcher / fixture-driven)
#   test_qram_e2e                        (G1 end-to-end stub gate)
#   test_qram_e2e_real                   (sturm-ddgo prod-headers gate;
#                                         ctest entry registered only under
#                                         STURM_FULL_TEST_SUITE — see below)
enable_language(C)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_executable(test_bucket_qram
    test_matcher_qram_subscript.cpp
    test_matcher_qram_subscript_assign.cpp
    test_matcher_qram_subscript_expr.cpp
    test_qram_emitter.cpp
    test_qram_emitter_assign.cpp
    test_qram_emitter_expr.cpp
    test_matcher_qram_oos.cpp
    test_qram_e2e.cpp
    test_qram_e2e_real.cpp
    bucket_dispatchers/test_bucket_qram_main.cpp
    $<TARGET_OBJECTS:sturm_transpile_test_objs>
    ${CMAKE_SOURCE_DIR}/src/sturm/qram/qram_read.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
)
sturm_define_bucket_target(test_bucket_qram)
target_include_directories(test_bucket_qram PRIVATE
    ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(test_bucket_qram PRIVATE
    STURM_QRAM_SUBSCRIPT_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    STURM_QRAM_EMITTER_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    STURM_QRAM_OOS_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    # sturm-ddgo (test_qram_e2e_real): production-header steering defines.
    # Only test_qram_e2e_real.cpp references these; benign for the others.
    STURM_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/include"
    STURM_VENDOR_DIR="${CMAKE_SOURCE_DIR}/vendor"
    STURM_CLANG_RESOURCE_DIR="${STURM_CLANG_RESOURCE_DIR}"
)
if(STURM_CLANG_LIBCXX_INCLUDE_DIR)
    target_compile_definitions(test_bucket_qram PRIVATE
        STURM_CLANG_LIBCXX_INCLUDE_DIR="${STURM_CLANG_LIBCXX_INCLUDE_DIR}")
endif()
target_link_libraries(test_bucket_qram PRIVATE orkan_headers)
# sturm-bs8s: mixed C+C++ target — gate_kind.c is linked in, but the
# transpiler's REUSE_FROM PCH is C++-only. CMake hard-errors when a
# mixed-language consumer attempts PCH reuse. Skip PCH for this bucket.
foreach(_t IN ITEMS
        test_matcher_qram_subscript
        test_qram_emitter
        test_matcher_qram_oos)
    add_test(NAME ${_t}
        COMMAND $<TARGET_FILE:test_bucket_qram> ${_t})
    set_tests_properties(${_t} PROPERTIES LABELS "transpiler;qram")
endforeach()
foreach(_t IN ITEMS
        test_matcher_qram_subscript_assign
        test_matcher_qram_subscript_expr
        test_qram_emitter_assign
        test_qram_emitter_expr
        test_qram_e2e)
    add_test(NAME ${_t}
        COMMAND $<TARGET_FILE:test_bucket_qram> ${_t})
    set_tests_properties(${_t} PROPERTIES
        LABELS "transpiler;qram;qram-subscript")
endforeach()
# sturm-eksf: test_qram_e2e_real ctest registration is gated behind
# STURM_FULL_TEST_SUITE. The binary always links (it shares the bucket
# target) so the build cost stays paid; only the ctest entry is gated
# per the original sturm-eksf posture.
if(STURM_FULL_TEST_SUITE)
    add_test(NAME test_qram_e2e_real
        COMMAND $<TARGET_FILE:test_bucket_qram> test_qram_e2e_real)
    set_tests_properties(test_qram_e2e_real PROPERTIES
        LABELS "transpiler;qram;qram-subscript")
endif()
unset(_t)

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
    # sturm-o1zj: P8 (sturm-yggr) migrated examples/qram_demo.cpp to the
    # opt-in `sturm/draw_ascii.h` header, which exposes the no-arg
    # `sturm::print_ascii()` / `sturm::gate_count()` entry points
    # implemented in draw_ascii_noarg.cpp. Mirror examples/CMakeLists.txt's
    # `example_qram_demo` source list so the rewritten TU links cleanly.
    "${CMAKE_SOURCE_DIR}/src/sturm/backend/draw_ascii_noarg.cpp"
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
    STURM_E1_RUNTIME_SOURCES="${_e1_runtime_sources_joined}"
    STURM_E1_ORKAN_INCLUDE_DIR="${_e1_orkan_include}"
    # sturm-zva0: forward the `STURM_MODE_DEFAULT=STURM_MODE_<X>` compile
    # def carried by `sturm::frontend` (sturm-mixe / P1) so the second-stage
    # clang++ that compiles the rewritten qram_demo.cpp resolves the
    # `sturm_backend_create(STURM_MODE_DEFAULT)` token emitted by the
    # auto-injected lifecycle (sturm-e3ru / P7).
    STURM_E1_MODE_DEFAULT_FLAG="-D$<JOIN:$<TARGET_PROPERTY:sturm_frontend,INTERFACE_COMPILE_DEFINITIONS>, -D>"
)
add_dependencies(test_qint_alias_subst_e2e sturm-transpile)
# sturm-eksf: gated behind STURM_FULL_TEST_SUITE.
if(STURM_FULL_TEST_SUITE)
    add_test(NAME test_qint_alias_subst_e2e
             COMMAND test_qint_alias_subst_e2e)
    set_tests_properties(test_qint_alias_subst_e2e PROPERTIES
        LABELS "transpiler;qac")
endif()

# ── Backend-script clean CI gate: test_sturm_gen_clean (sturm-7t85.4 / G4) ──
# Plan §19e, PRD §9.3.3 / G6 / A10. Walks the build's `sturm_gen/`
# directory after every `add_quantum_executable()` target has produced
# its rewritten output, and hard-fails on any line containing a
# `sturm::frontend::qint` mention (any spelling) or a
# `using qint = (::?)sturm::frontend::qint` typedef. Operationalises
# wave-1 G3 ("full alias erasure") as a permanent regression gate.
#
# The test resolves the build's `sturm_gen/` directory via the
# `<sturm_gen_path.hpp>` header generated by `configure_file` in
# `transpiler/CMakeLists.txt` (search for STURM_GEN_DIR). The
# `${CMAKE_BINARY_DIR}/include` path is added so the angle-bracket
# include resolves; the test is otherwise standalone (no LLVM linkage,
# no Clang headers) and links only against the C++ standard library.
add_executable(test_sturm_gen_clean
    test_sturm_gen_clean.cpp
)
target_compile_features(test_sturm_gen_clean PRIVATE cxx_std_20)
target_include_directories(test_sturm_gen_clean PRIVATE
    "${CMAKE_BINARY_DIR}/include"
)

# Dependency on every transpile-build target so the test fires only
# after `sturm_gen/` is materialised. Two layers:
#
#  1. `sturm-transpile` itself (always): the standalone driver — a
#     change here can alter every generated file, so it must be built
#     fresh before the gate runs.
#
#  2. Every `add_quantum_executable()` target: those are the producers
#     that materialise files under `sturm_gen/`. The list is collected
#     in two ways because of CMake's strict configure-time ordering
#     (root subdirectories: transpiler -> tests -> examples, all add_
#     quantum_executable callers live downstream of this subdir):
#       (a) Pre-existing entries in the global property
#           `_STURM_QUANTUM_EXECUTABLE_TARGETS` (zero today; non-zero
#           if a future re-shuffle moves any caller before the
#           transpiler subdir).
#       (b) A hook in `add_quantum_executable()` itself that calls
#           `add_dependencies(test_sturm_gen_clean ${target})` for
#           every target it creates AFTER this point. See the
#           `if(TARGET test_sturm_gen_clean)` block in
#           `cmake/SturmTranspile.cmake`.
#
# The two paths together guarantee the gate sees every transpile
# producer regardless of include order. If `sturm_gen/` is empty at
# test time the gate's directory-existence check fails loudly.
get_property(_sturm_qexec_targets GLOBAL
    PROPERTY _STURM_QUANTUM_EXECUTABLE_TARGETS)
if(_sturm_qexec_targets)
    add_dependencies(test_sturm_gen_clean
        sturm-transpile
        ${_sturm_qexec_targets})
else()
    add_dependencies(test_sturm_gen_clean sturm-transpile)
endif()

add_test(NAME test_sturm_gen_clean
         COMMAND test_sturm_gen_clean)
set_tests_properties(test_sturm_gen_clean PROPERTIES
    LABELS "transpiler;qac")

# ── Unit-level coverage of the scan logic (sturm-8a2q) ─────────────────────
# Drives `test_sturm_gen_clean_scan.hpp` with hand-crafted in-memory
# fixtures: positive-control regression (`qint a[4];` carrier-imported
# via `using sturm::qint;`) MUST trip; negative-controls (qualified
# `qint_t<W>`, comments referencing bare `qint`, the benign import
# line, `#include "sturm/qtypes/qint.hpp"`, `uncompute_*_qint` suffix)
# MUST stay clean. No filesystem walk, no transpile dependency — runs
# in milliseconds and acts as the canary that catches detector bugs
# before they hide a real residue regression.
add_executable(test_sturm_gen_clean_unit
    test_sturm_gen_clean_unit.cpp
)
target_compile_features(test_sturm_gen_clean_unit PRIVATE cxx_std_20)
add_test(NAME test_sturm_gen_clean_unit
         COMMAND test_sturm_gen_clean_unit)
set_tests_properties(test_sturm_gen_clean_unit PROPERTIES
    LABELS "transpiler;qac")
