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
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_hoist_invariant.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_ccnot_fuse.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_dead_ancilla.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_dropped_quantum_return.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_quantum_to_classical_cond.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/routine_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher_user_routine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/uncompute_pass.cpp
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
if(STURM_TRANSPILE_MODE STREQUAL "plugin" AND STURM_PLUGIN_CLANGXX_BIN)
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
    else()
        message(STATUS "sturm-transpile: pm4_smoke_dlopen skipped — "
                       "clang++ not found next to "
                       "LLVM_TOOLS_BINARY_DIR (STURM_PLUGIN_CLANGXX_BIN "
                       "unset). The test's verbose-replay leg needs a "
                       "reachable clang++ driver.")
    endif()
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
