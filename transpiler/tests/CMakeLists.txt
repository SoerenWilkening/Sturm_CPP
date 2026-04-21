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
    )
    add_dependencies(test_plugin_load sturm-transpile-plugin)
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
