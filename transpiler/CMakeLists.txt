# transpiler/CMakeLists.txt — builds the sturm-transpile driver (M4).
#
# Requires LLVM >= 17 and Clang >= 17. On systems where the packages are
# not installed the user will see a "find_package(LLVM) failed" error — we
# enrich that with an actionable hint below so debugging is a one-liner.

cmake_minimum_required(VERSION 3.16)

# ── PM4-10 option: statically link the demo plugin's registrar ──────────────
# When ON, the PM4-7 demo plugin's registrar is baked into
# `sturm-transpile-plugin` via the `STURM_REGISTER_PLUGIN` macro (link-time
# path, per docs/implementation_plan_transpiler_phase_m_pm4.md §5). This
# feeds the PM4-10 smoke test (`pm4_smoke_linktime`) and verifies the
# Meyer's-singleton drain end-to-end without relying on dlopen.
#
# Defaults to OFF so production builds keep `sturm-transpile-plugin` free
# of demo-plugin symbols. The option is read both here (to add the demo
# TUs to the plugin library sources) and in `tests/CMakeLists.txt`
# (to register the `pm4_smoke_linktime` CTest case, which depends on the
# registrar actually being linked in).
option(STURM_PM4_LINK_DEMO
    "Statically link the PM4-7 demo plugin registrar into \
sturm-transpile-plugin via STURM_REGISTER_PLUGIN (PM4-10 smoke test path)"
    OFF)
if(STURM_PM4_LINK_DEMO)
    message(STATUS "sturm-transpile: STURM_PM4_LINK_DEMO=ON — demo plugin "
                   "registrar baked into sturm-transpile-plugin "
                   "(pm4_smoke_linktime enabled)")
endif()

# ── Locate LLVM + Clang ───────────────────────────────────────────────────────
# find_package uses LLVM_DIR / Clang_DIR if provided. On Ubuntu 24.04 the
# relevant config files live under /usr/lib/llvm-17/lib/cmake/{llvm,clang}.
find_package(LLVM CONFIG)
if(NOT LLVM_FOUND)
    message(FATAL_ERROR
        "sturm-transpile requires LLVM >= 17 development headers.\n"
        "Install on Debian/Ubuntu:\n"
        "    sudo apt-get install llvm-17-dev libclang-17-dev clang-17\n"
        "and re-configure with:\n"
        "    cmake -DLLVM_DIR=/usr/lib/llvm-17/lib/cmake/llvm \\\n"
        "          -DClang_DIR=/usr/lib/llvm-17/lib/cmake/clang ..\n"
        "macOS: brew install llvm@17 (then set LLVM_DIR / Clang_DIR to the\n"
        "Homebrew prefix, e.g. /opt/homebrew/opt/llvm@17/lib/cmake/...).")
endif()
if(LLVM_PACKAGE_VERSION VERSION_LESS "17.0")
    message(FATAL_ERROR
        "sturm-transpile requires LLVM >= 17 but found ${LLVM_PACKAGE_VERSION}. "
        "Install llvm-17-dev or newer, then point LLVM_DIR at the matching "
        "cmake directory.")
endif()

find_package(Clang CONFIG)
if(NOT Clang_FOUND)
    message(FATAL_ERROR
        "sturm-transpile requires Clang >= 17 development headers.\n"
        "On Debian/Ubuntu run:\n"
        "    sudo apt-get install libclang-17-dev clang-17\n"
        "and re-configure with:\n"
        "    cmake -DClang_DIR=/usr/lib/llvm-17/lib/cmake/clang ..")
endif()

message(STATUS "sturm-transpile: using LLVM ${LLVM_PACKAGE_VERSION} "
               "from ${LLVM_INSTALL_PREFIX}")

# Promote LLVM_INSTALL_PREFIX to a cache var so cmake/SturmTranspile.cmake
# (included from the root, outside this subdirectory's variable scope) can
# reach it. Needed for the dump-mode `-cxx-isystem` plumbing (sturm-r1c1):
# libTooling parses must use llvm@17's bundled libc++ headers, not the
# host SDK's, when the SDK is newer than the parser supports.
set(STURM_LLVM_INSTALL_PREFIX "${LLVM_INSTALL_PREFIX}" CACHE INTERNAL
    "Resolved LLVM install prefix (set by find_package(LLVM)). \
Forwarded to SturmTranspile.cmake for dump-mode --extra-arg plumbing.")

# ── sturm-transpile binary ────────────────────────────────────────────────────
add_executable(sturm-transpile
    src/main.cpp
    src/transpile_consumer.cpp
    src/diag_context.cpp
    src/io.cpp
    src/skip.cpp
    src/qir.cpp
    src/matcher_qbool_bitwise.cpp
    src/matcher_qbool_compound.cpp
    src/matcher_qbool_assign.cpp
    src/matcher_qint_const.cpp
    src/matcher_qint_qint.cpp
    src/matcher_qint_compare.cpp
    # Phase N PN-2: rotation compound-assign matchers (theta / phi +=/-=).
    # Dogfooded into the pipeline via `STURM_REGISTER_PLUGIN(PNRotationPlugin)`;
    # the `TranspileConsumer`'s Registry drain picks them up at static-init
    # time, so there is no direct consumer-side registration call.
    src/matcher_rotation.cpp
    src/matcher_when_lift.cpp
    src/matcher_when_nested.cpp
    src/matcher_when_operand_mutation.cpp
    # E7 (sturm-va3z): WHEN free-variable mutation diagnostic. M1 = read-
    # set extractor, M2 = write-set checker, M3 = matcher wiring. Linked
    # into the standalone driver here so the consumer's
    # `register_when_freevar_matcher` call resolves at link time.
    src/when_freevar_readset.cpp
    src/when_freevar_check.cpp
    src/matcher_when_freevar.cpp
    src/matcher_quantum_to_classical_cond.cpp
    src/matcher_brace_wrap.cpp
    src/matcher_outer_var_guard.cpp
    # Phase S S-B (sturm-ha2k.3): matcher_outer_var_guard.cpp now calls
    # `is_reversible()` from reversible_attribute.cpp to gate the Phase
    # S loop-reversal handoff on `[[sturm::reversible]]`. Link the TU so
    # the main binary resolves the symbol. reversible_attribute.cpp is a
    # small predicate whose only Clang deps (AnnotateAttr) are already
    # on the link line via clangAST / clangBasic.
    src/reversible_attribute.cpp
    # Phase T T-1 (sturm-xrob.2): the Phase P/Q/R/S synthesis modules
    # land in the main sturm-transpile binary here. Before T-1 these
    # TUs were only compiled under `transpiler/tests/` for their
    # individual unit tests; the consumer wiring now activates them
    # end-to-end — every `[[clang::annotate("sturm::reversible")]]`
    # forward is collected by `register_reversible_drive_matcher` and
    # driven by `drive_reversible_forwards` from
    # `transpile_consumer.cpp`. Link order is irrelevant because each
    # TU declares its symbols in `sturm::transpile` and the public
    # headers forward-declare the pieces the consumer needs.
    src/synthesis_registry.cpp
    src/matcher_reversible_validate.cpp
    src/matcher_reversible_signature.cpp
    src/return_to_out_param.cpp
    src/adjoint_emitter.cpp
    src/auto_register_emitter.cpp
    src/matcher_reversible_drive.cpp
    src/loop_reversal.cpp
    # Phase N PN-5: qbool(p) prep diagnostic matcher. Anchors on
    # qbool VarDecls whose initializer is a probabilistic `double`
    # and whose lexically enclosing scope is uncompute-eligible
    # (a WHEN body or a compound-expression intermediate). Routes
    # through the shared `DiagContext` — same pattern as PH-3's
    # outer-var guard (matcher_outer_var_guard.cpp above).
    src/matcher_qbool_prep.cpp
    src/matcher_hoist_invariant.cpp
    src/matcher_ccnot_fuse.cpp
    src/matcher_dead_ancilla.cpp
    src/matcher_dropped_quantum_return.cpp
    src/routine_registry.cpp
    src/matcher_user_routine.cpp
    src/uncompute_pass.cpp
    # sturm-v0ur (LO-2 wiring): per-op inverse renderer hoisted from
    # `uncompute_pass.cpp` so that file stays under the plan §9 600-LoC
    # budget once the `register_external_cleanup()` hook lands.
    src/uncompute_render.cpp
    # sturm-v0ur (LO-2 wiring): lossy compound-assign rewrite pipeline.
    # `matcher_lossy_op` (LO-2a) emits `LossyOpHit`s for `qint *=`,
    # `/=`, `%=`, `&=`, `|=`; `lossy_rewrite_emitter` (LO-2b) turns
    # each hit into the §2.2 forward triplet (allocate-compute-swap);
    # `lossy_scope_exit_emitter` (LO-2c) groups hits per enclosing
    # `CompoundStmt` and emits the §2.3 LIFO cleanup. The wiring lives
    # in `transpile_consumer.cpp`; the LO-2d main-outer suppression is
    # baked into `lossy_scope_exit_emitter` itself.
    src/matcher_lossy_op.cpp
    src/lossy_rewrite_emitter.cpp
    src/lossy_scope_exit_emitter.cpp
    # sturm-rry6 (LO-2e): depth-first nested-lossy rewrite. Adds an AST
    # matcher for `a *= (b & c)` outer-lossy / inner-bare-binary shapes
    # (LO-2a's matcher requires a bare DRE on arg(1) so it does not
    # fire on these). The consumer drains nested hits ahead of plain
    # LO-2a hits and produces the inner-first forward + outer-first
    # cleanup pair via `emit_nested_lossy`.
    src/lossy_nested_rewrite.cpp
    # sturm-r5pn.4 (Phase 0.4): modular-arith matcher + emitter SHELL.
    # Both TUs land empty (matcher registers no patterns; emitter
    # returns empty `ModularEmission`s) so that Phase 5
    # (P5 transpiler-modular-rewrite) can swap in the per-pattern
    # bodies without touching the source-list ordering or any
    # consumer-side wiring. The identity snapshot fixtures
    # `tests/transpiler/fixtures/modular_{add,mul}_op.{cpp,expected.cpp}`
    # plus the pow flag-default / flag-on pair pin the round-trip
    # contract while the bodies are empty. See plan §2.1 / §7.
    src/matcher_modular_op.cpp
    src/matcher_modular_compound_collapse.cpp
    src/matcher_modular_pow_mod.cpp
    src/modular_rewrite_emitter.cpp
    # sturm-ddgo: QRAM-via-array-subscript matcher + emitter pipeline.
    # Three matchers (C1 bare-init, H1 existing-target, H4 expression-
    # position) and three companion emitters; the consumer's drain
    # block converts each matched hit into `QReplacement` /
    # `UncomputeInsertion` records that flow through the standard
    # `synthesize()` / `emit()` path. `width_inference.cpp` is the
    # shared B1 width-inference helper consulted by all three matchers
    # (PRD §11.3 / D0c.3 single source of truth).
    src/matcher_qram_subscript.cpp
    src/matcher_qram_subscript_assign.cpp
    src/matcher_qram_subscript_expr.cpp
    src/qram_emitter.cpp
    src/qram_emitter_assign.cpp
    src/qram_emitter_expr.cpp
    src/width_inference.cpp
    # sturm-65rs.8 (Beat C1): qint alias substitution matcher. Anchored
    # on every source site that spells `sturm::frontend::qint` (VarDecl,
    # ParmVarDecl, FieldDecl, function return type, CXXFunctionalCastExpr).
    # The C2 emitter (sturm-65rs.9) consumes the typed Match struct;
    # consumer wiring happens in C3 (sturm-65rs.10). At this beat the TU
    # is link-resolved here so the existing test binaries (test_transpile_
    # consumer + plugin) keep linking once C3 wires it into the consumer.
    src/matcher_qint_alias_subst.cpp
    # sturm-7t85.1 (Beat G1): single-level array-element / pointer-pointee
    # carrier walk + R5 typedef-decline. Sibling TU split off the matcher
    # to keep the matcher source ≤ 300 LoC (plan §19b).
    src/qint_alias_carrier_walk.cpp
    # sturm-65rs.9 (Beat C2): qint alias substitution emitter. Consumes
    # the C1 matcher's `QintAliasSubstMatch` vector and rewrites every
    # captured `TypeLoc` to `sturm::qint_t<W>` (W via `infer_width()`
    # for VarDecls; `kDefaultWidth = 32` for the four other anchors per
    # PRD §3 non-goal). Linked here so the C3 consumer (sturm-65rs.10)
    # wiring resolves cleanly.
    src/qint_alias_subst_emitter.cpp
    # sturm-e3ru (Frontend simpl. P7) + sturm-0tcv (entry-point attribute
    # extension): main_lifecycle matcher + emitter. The matcher fires on
    # (a) the unique `int main(...)` FunctionDecl in TUs where the
    # umbrella sentinel (STURM_UMBRELLA_INCLUDED) is defined and the
    # escape hatch (STURM_NO_AUTO_LIFECYCLE) is not, and (b) any
    # FunctionDecl carrying the `[[sturm::entry_point]]` attribute.
    # The companion emitter rewrites the body into the IIFE form
    # (PRD §5.4 listing). `entry_point_attribute.cpp` exports the
    # detection primitive `has_entry_point_attr` consumed by the
    # matcher.
    src/matcher_main_lifecycle.cpp
    src/main_lifecycle_emitter.cpp
    src/entry_point_attribute.cpp
    src/source_map.cpp
    src/emitter.cpp
    # PM4-2: Registry implementation + Meyer's-singleton vector for
    # link-time registrars. Plugin-path matchers (PM4-4 dlopen, PM4-3
    # in-process Registry& threading) resolve into this TU.
    src/plugin_registry.cpp
    # PM5-2 / PM5-3: alias-analysis extractor + overlap predicate.
    # The peephole reorder matcher (PM5-5) consumes this TU via the
    # public `sturm/transpile/alias.hpp` surface.
    src/alias.cpp
    # PM5-5: peephole gate-reordering matcher. Post-processor that
    # commutes a disjoint-footprint op B past a fuse-candidate pair
    # (A, C) so the PJ-1 fuse peephole can absorb (A, C) on the next
    # pass. Consumes `sturm/transpile/alias.hpp` for footprint
    # extraction and `matcher_common.hpp` for the shared scope-kind /
    # reader-count / fused-range helpers. Registration in
    # `transpile_consumer.cpp` LAST (after `register_hoist_invariant_
    # matcher`) is a separate issue (PM5-6).
    src/matcher_peephole_reorder.cpp
)
target_include_directories(sturm-transpile PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
# LLVM headers do not tolerate RTTI mismatches; inherit LLVM's own setting.
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(sturm-transpile PRIVATE -fno-rtti)
endif()
target_compile_features(sturm-transpile PRIVATE cxx_std_20)

# ── sturm-2vbr — Position-Independent-Code on the driver ───────────────────
# The PCH the driver writes is REUSE_FROM'd by the sturm-transpile-plugin
# SHARED library below. CMake REUSE_FROM enforces matching `target_compile_*`
# settings between the two targets, but it does NOT normalize the compiler
# default PIE state per target type. On Linux clang++-17:
#
#   * executables default to `-fpie`; CMake's POSITION_INDEPENDENT_CODE ON
#     on an executable adds `-fPIE` (NOT `-fPIC`). Either flavor of PIE
#     tags the PCH with `is pie = true`.
#   * SHARED libraries are forced to `-fPIC` (`is pie = false`),
#     regardless of POSITION_INDEPENDENT_CODE.
#
# Loading the driver's `is pie = true` PCH from the plugin's `-fPIC`
# compile then trips:
#
#     error: is pie differs in PCH file vs. current file
#
# Setting POSITION_INDEPENDENT_CODE ON alone is therefore insufficient —
# the executable would still get `-fPIE` (`is pie = true`). We need to
# force `-fPIC` (`is pie = false`) on the driver to match the plugin's
# PCH consumer state. `target_compile_options(... -fPIC)` is the
# narrowest knob that works on Linux without reaching for the
# CMAKE_<LANG>_COMPILE_OPTIONS_PIE override:
#
#   * Linux clang++-17: `-fPIC` produces a PIC executable with
#     `is pie = false`, matching the plugin.
#   * macOS clang (Apple or Homebrew LLVM 17): `-fPIC` is a no-op (PIC
#     is the only supported relocation model on Mach-O), so nothing
#     changes vs. the current macOS build.
#
# The driver remains an executable; PIC executables are first-class on
# every modern Linux distro. The PCH's `is pie = false` then matches
# the plugin's compile context, and REUSE_FROM resolves cleanly.
# Pinned by the `cmake_pch_pie_match` ctest in
# `tests/cmake_pch_pie_match.cmake`.
target_compile_options(sturm-transpile PRIVATE -fPIC)
# LLVM's definition list (LLVM_DEFINITIONS) carries the flags the headers
# were compiled with. Without these the Clang headers may see a different
# configuration than the library and fail to link.
separate_arguments(LLVM_DEFS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(sturm-transpile PRIVATE ${LLVM_DEFS_LIST})

# Bake the Clang builtin-headers resource directory into the binary so the
# LibTooling driver can find stdarg.h / stddef.h at runtime. Without this,
# every transpile on macOS (and any environment where LibTooling cannot
# locate its own builtins via argv[0]) fails to resolve __uint32_t /
# __darwin_wint_t in libc++ sys headers, the parser abandons main-file
# translation before the user's body is built into the AST, and every Phase
# A-I matcher sees zero ops from the user's code. The transpile still
# succeeds (emits a pass-through), so the breakage is silent — hence the
# compile-time capture here.
#
# The layout varies across distributions:
#   - macOS Homebrew llvm@17:     <prefix>/lib/clang/<N>
#   - Ubuntu apt-installed clang: /usr/lib/clang/<N>   (NOT under llvm-17/)
#   - From-source LLVM build:     <build>/lib/clang/<N>
# The authoritative answer for each layout is `clang -print-resource-dir`,
# so we ask the actual Clang binary installed alongside the package we
# linked against. Falls back to `${LLVM_INSTALL_PREFIX}/lib/clang/<N>` if
# no clang binary is findable (e.g. truly static-only LLVM builds).
find_program(STURM_CLANG_BIN
    NAMES "clang-${LLVM_VERSION_MAJOR}" "clang"
    HINTS "${LLVM_TOOLS_BINARY_DIR}"
    NO_DEFAULT_PATH)
if(STURM_CLANG_BIN)
    execute_process(
        COMMAND "${STURM_CLANG_BIN}" -print-resource-dir
        OUTPUT_VARIABLE STURM_CLANG_RESOURCE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(WARNING "sturm-transpile: `${STURM_CLANG_BIN} "
                        "-print-resource-dir` exited non-zero (${_rc}); "
                        "falling back to computed path.")
        unset(STURM_CLANG_RESOURCE_DIR)
    endif()
endif()
if(NOT STURM_CLANG_RESOURCE_DIR)
    set(STURM_CLANG_RESOURCE_DIR
        "${LLVM_INSTALL_PREFIX}/lib/clang/${LLVM_VERSION_MAJOR}")
endif()
message(STATUS "sturm-transpile: Clang resource dir = "
               "${STURM_CLANG_RESOURCE_DIR}")
target_compile_definitions(sturm-transpile PRIVATE
    STURM_CLANG_RESOURCE_DIR="${STURM_CLANG_RESOURCE_DIR}"
    STURM_LLVM_VERSION_MAJOR_STR="${LLVM_VERSION_MAJOR}")

# sturm-yi9l / sturm-fbeb: bake the path to the LLVM-install's bundled
# libc++ headers into the binary so the libTooling driver can override
# the host SDK's libc++ when the SDK ships headers newer than the
# parser supports. Apple's MacOSX26.sdk libc++ uses Clang >=19 builtins
# (__builtin_clzg / __builtin_ctzg / _LIBCPP_NO_SPECIALIZATIONS) but
# `sturm-transpile` is linked against libclang-17 / libLLVM-17 — parsing
# SDK 26 headers under that older parser yields a wall of "use of
# undeclared identifier" / "does not refer to a value" errors before
# any matcher fires. Routing the parse through
# `${LLVM_INSTALL_PREFIX}/include/c++/v1` (which DOES match the parser
# version) bypasses the SDK headers entirely.
#
# Mirrors the plumbing in cmake/SturmTranspile.cmake's dump-mode
# `--extra-arg=-cxx-isystem...` injection (sturm-r1c1) but applies it at
# the C++ tool level so EVERY libTooling invocation of `sturm-transpile`
# benefits — including direct shell-out from tests like
# test_plugin_nested.cpp where the build-system glue is bypassed.
#
# Guarded behind `EXISTS` so an LLVM distribution without bundled libc++
# headers (e.g. a stripped Linux install) degrades to the previous
# behaviour — under those distributions the system libc++ is what the
# parser was built against, so no override is needed.
set(STURM_CLANG_LIBCXX_INCLUDE_DIR "")
if(EXISTS "${LLVM_INSTALL_PREFIX}/include/c++/v1")
    set(STURM_CLANG_LIBCXX_INCLUDE_DIR
        "${LLVM_INSTALL_PREFIX}/include/c++/v1")
    message(STATUS "sturm-transpile: bundled libc++ include = "
                   "${STURM_CLANG_LIBCXX_INCLUDE_DIR}")
    target_compile_definitions(sturm-transpile PRIVATE
        STURM_CLANG_LIBCXX_INCLUDE_DIR="${STURM_CLANG_LIBCXX_INCLUDE_DIR}")
endif()

# ── sturm-r5pn.3: STURM_MODULAR_POW propagation (driver-only) ──────────────
# Plan §2.2 / PRD §3.4: when the top-level option `STURM_MODULAR_POW`
# is ON, propagate it as a preprocessor define to the transpiler
# driver target so the Phase 5 matcher can `#ifdef STURM_MODULAR_POW`
# its `pow(a, x) % n => lib_pow_mod_dsl` rewrite path. The define is
# intentionally NOT applied to:
#   - `sturm` (the header-only INTERFACE library) — modular primitives
#     are compiled unconditionally; only the rewrite is gated;
#   - `sturm-transpile-plugin` — the plugin currently shares the same
#     consumer pipeline as the standalone driver but does not yet
#     embed the modular matcher; when Phase 5 wires it in, a
#     parallel block here will mirror the propagation. Keeping the
#     plugin out today preserves the contract pinned by
#     `cmake_modular_pow_flag` (driver-only propagation);
#   - any test or example target — those exercise the lib layer
#     directly and the lib layer is flag-agnostic.
# The `cmake_modular_pow_flag` ctest (tests/cmake_modular_pow_flag.cmake)
# pins this contract by inspecting `compile_commands.json` for both
# `STURM_MODULAR_POW=ON` and `OFF` configures.
if(STURM_MODULAR_POW)
    target_compile_definitions(sturm-transpile PRIVATE STURM_MODULAR_POW)
endif()

# Link against the shared libclang-cpp + libLLVM when available. The
# distro-shipped LLVM layout (Debian / Ubuntu / Homebrew `llvm@17`)
# contains both static component archives (libclangTooling.a, ...) AND a
# combined shared library (libclang-cpp.so, libLLVM-17.so). If we link
# the static archives AND anything that itself was built against the
# shared library, LLVM's cl::opt global table ends up registered twice
# at process start and the tool aborts with:
#     Option 'debug-counter' registered more than once!
# The robust fix is to pick exactly ONE: prefer the shared libs, which
# is also how upstream clang-tools (`clang-tidy`, `clang-format`) link.
# Fall back to the static per-component archives if the shared library
# cannot be located — some custom LLVM builds ship static-only.
# Debian/Ubuntu ships libclang-cpp without a bare ".so" symlink — the
# file is `libclang-cpp.so.${LLVM_VERSION_MAJOR}`. The default find_library
# soname list does not cover that. Try the versioned name first, then fall
# back to the canonical name, then to a brute-force existence check.
find_library(STURM_LIBCLANG_CPP
    NAMES "clang-cpp-${LLVM_VERSION_MAJOR}"
          "clang-cpp"
    HINTS ${LLVM_LIBRARY_DIR}
    NO_DEFAULT_PATH)
if(NOT STURM_LIBCLANG_CPP)
    # Try the filename directly — CMake recognizes a fully specified path.
    set(_cand "${LLVM_LIBRARY_DIR}/libclang-cpp.so.${LLVM_VERSION_MAJOR}")
    if(EXISTS "${_cand}")
        set(STURM_LIBCLANG_CPP "${_cand}" CACHE FILEPATH
            "Path to libclang-cpp shared library" FORCE)
    endif()
endif()

find_library(STURM_LIBLLVM
    NAMES "LLVM-${LLVM_VERSION_MAJOR}"
          "LLVM"
    HINTS ${LLVM_LIBRARY_DIR}
    NO_DEFAULT_PATH)
if(NOT STURM_LIBLLVM)
    set(_cand "${LLVM_LIBRARY_DIR}/libLLVM-${LLVM_VERSION_MAJOR}.so")
    if(EXISTS "${_cand}")
        set(STURM_LIBLLVM "${_cand}" CACHE FILEPATH
            "Path to libLLVM shared library" FORCE)
    endif()
endif()

if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    message(STATUS "sturm-transpile: linking shared libclang-cpp + libLLVM "
                   "(${STURM_LIBCLANG_CPP}, ${STURM_LIBLLVM})")
    target_link_libraries(sturm-transpile PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    message(STATUS "sturm-transpile: shared libclang-cpp / libLLVM not found, "
                   "falling back to per-component static libraries")
    target_link_libraries(sturm-transpile PRIVATE
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
    # LLVMSupport carries cl::opt, Expected, errs(), etc.
    llvm_map_components_to_libnames(LLVM_LIBS support)
    target_link_libraries(sturm-transpile PRIVATE ${LLVM_LIBS})
endif()

# ── Plugin: sturm-transpile-plugin (PM1-3) ────────────────────────────────────
# Clang plugin (.so / .dylib) that drives the shared TranspileConsumer in
# EmissionMode::Plugin. Loaded via `clang++ -fplugin=$<TARGET_FILE:...>`
# on a downstream compile, reuses the same matcher pool as the standalone
# binary, and (after PM1-4 wires nested codegen) substitutes the
# transpiler-rewritten source for the user's raw input before EmitObjAction.
#
# Link discipline mirrors the main binary: shared libclang-cpp + libLLVM
# when available (to avoid cl::opt double-registration — Phase L /
# sturm-mzfa lesson), fallback to the static per-component archives
# otherwise. Symbol hiding is enforced on Linux via
# -Wl,--exclude-libs,ALL so the tens of thousands of symbols the
# shared LLVM .so exposes do not leak out of OUR plugin's interface
# (Clang's loader only needs the FrontendPluginRegistry Add<> ctor).
# On macOS, two-level namespace lookup already prevents symbol
# collision between dlopen'd plugins and the host clang's copy of
# LLVM, so no extra flag is required.
#
# The file is built as SHARED (not MODULE) so the target exports a
# normal .so/.dylib — Clang's plugin loader treats the two file types
# interchangeably and SHARED keeps the TARGET_FILE path consistent on
# both Linux (libsturm-transpile-plugin.so) and macOS
# (libsturm-transpile-plugin.dylib). Tests reach the file via the
# $<TARGET_FILE:sturm-transpile-plugin> generator expression, so a
# rename never needs to touch the test fixtures.
add_library(sturm-transpile-plugin SHARED
    src/plugin.cpp
    src/transpile_consumer.cpp
    src/diag_context.cpp
    src/io.cpp
    src/skip.cpp
    src/qir.cpp
    src/matcher_qbool_bitwise.cpp
    src/matcher_qbool_compound.cpp
    src/matcher_qbool_assign.cpp
    src/matcher_qint_const.cpp
    src/matcher_qint_qint.cpp
    src/matcher_qint_compare.cpp
    # Phase N PN-2: rotation compound-assign matchers. Mirror the main
    # binary so the plugin-path consumer (same `TranspileConsumer`) links
    # the same symbols. Registration flows through the Registry drain
    # (`STURM_REGISTER_PLUGIN(PNRotationPlugin)` at the bottom of
    # `matcher_rotation.cpp`); no direct consumer-side wiring.
    src/matcher_rotation.cpp
    src/matcher_when_lift.cpp
    src/matcher_when_nested.cpp
    src/matcher_when_operand_mutation.cpp
    # E7 (sturm-va3z): mirror the main binary — the plugin-path consumer
    # (same TranspileConsumer) calls `register_when_freevar_matcher` and
    # needs the M1 / M2 helpers + M3 matcher resolved at dlopen.
    src/when_freevar_readset.cpp
    src/when_freevar_check.cpp
    src/matcher_when_freevar.cpp
    src/matcher_quantum_to_classical_cond.cpp
    src/matcher_brace_wrap.cpp
    src/matcher_outer_var_guard.cpp
    # Phase S S-B (sturm-ha2k.3): mirror the main binary — the plugin-
    # path build needs `is_reversible()` resolved for the S-B opt-in
    # in `matcher_outer_var_guard.cpp`.
    src/reversible_attribute.cpp
    # Phase T T-1 (sturm-xrob.2): mirror the main binary so the plugin
    # build links the same synthesis pipeline the consumer now drives
    # via `drive_reversible_forwards`. See the equivalent block above.
    src/synthesis_registry.cpp
    src/matcher_reversible_validate.cpp
    src/matcher_reversible_signature.cpp
    src/return_to_out_param.cpp
    src/adjoint_emitter.cpp
    src/auto_register_emitter.cpp
    src/matcher_reversible_drive.cpp
    src/loop_reversal.cpp
    # Phase N PN-5: qbool(p) prep diagnostic matcher. Mirror the main
    # binary so the plugin-path consumer (same `TranspileConsumer`)
    # links the same symbols. Registration order in
    # `transpile_consumer.cpp` (AFTER `register_outer_var_guard_matcher`)
    # is PN-5's contract.
    src/matcher_qbool_prep.cpp
    src/matcher_hoist_invariant.cpp
    src/matcher_ccnot_fuse.cpp
    src/matcher_dead_ancilla.cpp
    src/matcher_dropped_quantum_return.cpp
    src/routine_registry.cpp
    src/matcher_user_routine.cpp
    src/uncompute_pass.cpp
    # sturm-v0ur (LO-2 wiring): mirror the main binary — the plugin
    # build needs `render_uncompute()` resolved from the new TU and
    # the lossy-rewrite pipeline (LO-2a/2b/2c/2d) linked in.
    src/uncompute_render.cpp
    src/matcher_lossy_op.cpp
    src/lossy_rewrite_emitter.cpp
    src/lossy_scope_exit_emitter.cpp
    # sturm-rry6 (LO-2e): depth-first nested-lossy rewrite. Adds an AST
    # matcher for `a *= (b & c)` outer-lossy / inner-bare-binary shapes
    # (LO-2a's matcher requires a bare DRE on arg(1) so it does not
    # fire on these). The consumer drains nested hits ahead of plain
    # LO-2a hits and produces the inner-first forward + outer-first
    # cleanup pair via `emit_nested_lossy`.
    src/lossy_nested_rewrite.cpp
    # sturm-r5pn.4 (Phase 0.4): mirror the main binary — the plugin
    # build needs the modular-arith matcher + emitter SHELL TUs linked
    # in so that Phase 5's per-pattern bodies resolve identically when
    # the plugin-path consumer constructs its `register_modular_op_
    # matcher` callback. SHELL = empty bodies; the symbol surface is
    # the contract under test.
    src/matcher_modular_op.cpp
    src/matcher_modular_compound_collapse.cpp
    src/matcher_modular_pow_mod.cpp
    src/modular_rewrite_emitter.cpp
    # sturm-ddgo: mirror the main binary — the plugin build needs the
    # QRAM matchers + emitters resolved at dlopen so the
    # plugin-path consumer (same `TranspileConsumer`) registers and
    # drains them identically.
    src/matcher_qram_subscript.cpp
    src/matcher_qram_subscript_assign.cpp
    src/matcher_qram_subscript_expr.cpp
    src/qram_emitter.cpp
    src/qram_emitter_assign.cpp
    src/qram_emitter_expr.cpp
    src/width_inference.cpp
    # sturm-65rs.8 (Beat C1): qint alias substitution matcher. Mirror
    # the main binary so the plugin-path consumer resolves the same
    # symbols at dlopen — see the header note above the main binary's
    # source-list entry for details.
    src/matcher_qint_alias_subst.cpp
    # sturm-7t85.1 (Beat G1): carrier walk sibling. Mirror the main
    # binary so the plugin-path consumer (TranspileConsumer) links the
    # same `declarator_typeloc_range` symbol at dlopen.
    src/qint_alias_carrier_walk.cpp
    # sturm-65rs.9 (Beat C2): qint alias substitution emitter. Mirror
    # the main binary so the plugin-path consumer (same TranspileConsumer)
    # links the same emitter symbols at dlopen.
    src/qint_alias_subst_emitter.cpp
    # sturm-e3ru (Frontend simpl. P7) + sturm-0tcv (entry-point attribute
    # extension): main_lifecycle matcher + emitter. Mirror the main
    # binary so the plugin-path consumer (same TranspileConsumer)
    # registers and drains the matcher identically at dlopen.
    # `entry_point_parsed_attr_info.cpp` registers a Clang
    # ParsedAttrInfo so the plain `[[sturm::entry_point]]` spelling
    # is accepted alongside the canonical
    # `[[clang::annotate("sturm::entry_point")]]` carrier; the plugin
    # build is the only place where the ParsedAttrInfoRegistry is
    # consumed (the standalone driver also benefits — both link the
    # registration TU, but only the plugin path's host-clang ever
    # sees the attribute syntax in user source).
    src/matcher_main_lifecycle.cpp
    src/main_lifecycle_emitter.cpp
    src/entry_point_attribute.cpp
    src/entry_point_parsed_attr_info.cpp
    src/source_map.cpp
    src/emitter.cpp
    # PM4-2: Registry implementation + Meyer's-singleton vector for
    # link-time registrars. Plugin-path matchers (PM4-4 dlopen, PM4-3
    # in-process Registry& threading) resolve into this TU.
    src/plugin_registry.cpp
    # PM5-2 / PM5-3: alias-analysis extractor + overlap predicate.
    # Mirror the main binary so the plugin-path peephole matcher
    # (PM5-5) resolves the same symbols at dlopen.
    src/alias.cpp
    # PM5-5: peephole gate-reordering matcher. Mirror the main binary
    # so the plugin-path consumer (same TranspileConsumer) links the
    # same symbols. Registration order in `transpile_consumer.cpp`
    # (LAST, after `register_hoist_invariant_matcher`) is PM5-6's
    # concern; PM5-5 ships only the source + the header export.
    src/matcher_peephole_reorder.cpp
)

# PM4-10: when `STURM_PM4_LINK_DEMO=ON`, bake the demo plugin's registrar
# into `sturm-transpile-plugin` via the `STURM_REGISTER_PLUGIN` macro.
# The shim TU (`pm4_link_demo_registrar.cpp`) provides the namespace-
# scope `StaticRegistrar` that forwards into the demo's
# `sturm_register_plugin_v1` entry point; the demo's own TU
# (`examples/plugin_demo/plugin_demo.cpp`) provides that entry point plus
# the `render_tag_inverse` renderer. Both TUs must be compiled together
# so the forward declaration in the shim resolves at link time.
#
# The option is OFF by default; when OFF, neither TU is compiled into
# the plugin library and production builds stay demo-free.
if(STURM_PM4_LINK_DEMO)
    target_sources(sturm-transpile-plugin PRIVATE
        ${CMAKE_SOURCE_DIR}/examples/plugin_demo/plugin_demo.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/pm4_link_demo_registrar.cpp
    )
endif()

target_include_directories(sturm-transpile-plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(sturm-transpile-plugin PRIVATE -fno-rtti)
endif()
target_compile_features(sturm-transpile-plugin PRIVATE cxx_std_20)
target_compile_definitions(sturm-transpile-plugin PRIVATE
    ${LLVM_DEFS_LIST}
    STURM_CLANG_RESOURCE_DIR="${STURM_CLANG_RESOURCE_DIR}"
    STURM_LLVM_VERSION_MAJOR_STR="${LLVM_VERSION_MAJOR}")

if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(sturm-transpile-plugin PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(sturm-transpile-plugin PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_PLUGIN support)
    target_link_libraries(sturm-transpile-plugin PRIVATE ${LLVM_LIBS_PLUGIN})
endif()

# PM4-4: `plugin.cpp`'s `load=<path>` dispatch calls `dlopen` / `dlsym` /
# `dlerror` from `<dlfcn.h>`. On glibc >= 2.34 those live directly in
# libc.so.6 and no extra link is needed; on older glibc they live in
# libdl.so.2. `CMAKE_DL_LIBS` resolves to the right thing on both and
# expands to empty on platforms where the dynamic-loader symbols are
# intrinsic (macOS darwin).
target_link_libraries(sturm-transpile-plugin PRIVATE ${CMAKE_DL_LIBS})

# Hide the transitively-pulled LLVM symbols from the plugin's exported
# interface on Linux. A no-op on macOS / Darwin, where two-level
# namespace lookup already keeps the plugin's LLVM copy from colliding
# with the host clang's copy.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Hide the transitively-pulled LLVM symbols from the plugin's
    # EXPORTED interface. This prevents the tens of thousands of LLVM
    # / Clang symbols from leaking out of the plugin .so while still
    # letting the plugin RESOLVE those symbols (via its DT_NEEDED
    # libLLVM-17.so / libclang-cpp.so entries) — exclude-libs only
    # touches the dynamic symbol TABLE, not runtime lookup.
    target_link_options(sturm-transpile-plugin PRIVATE
        "LINKER:--exclude-libs,ALL")
endif()

# Clang's plugin loader matches on basename; we keep the conventional
# `lib` prefix here because `-fplugin=<full-path>` takes an absolute
# path and does not care about the prefix. The target-file generator
# expression (`$<TARGET_FILE:sturm-transpile-plugin>`) resolves to the
# right thing on every platform without bespoke PREFIX massaging.

# ── Precompiled headers (sturm-cxrn) ──────────────────────────────────────────
# Build acceleration: each transpiler TU otherwise re-parses the same heavy
# clang/llvm umbrella headers from scratch. Source-survey hit counts across
# transpiler/src + transpiler/include (51 TUs total): clang/AST/{Expr,Decl,
# ASTContext}.h = 49/51, ASTMatchers/ASTMatchFinder.h = 48, Basic/Source
# Location.h = 44, AST/Stmt.h = 41, Basic/SourceManager.h = 37, AST/ExprCXX.h
# = 36, ASTMatchers/ASTMatchers.h = 34, Lex/Lexer.h = 23, AST/{Type,DeclCXX}.h
# = 20 each, Basic/OperatorKinds.h = 18, AST/{RecursiveASTVisitor,ParentMap
# Context}.h = 16 each, Basic/LangOptions.h = 14, Rewrite/Core/Rewriter.h = 7.
# A PCH built from this set is reused by the overwhelming majority of TUs,
# parsed once per build instead of ~50×.
#
# The plugin target REUSE_FROM the driver's PCH so the cost is paid exactly
# once per build (CMake enforces matching compile options between reuser
# and source target — both targets share -fno-rtti, C++20, and the same
# LLVM_DEFINITIONS / STURM_CLANG_RESOURCE_DIR / STURM_LLVM_VERSION_MAJOR_STR
# defines, so the constraint holds). STURM_MODULAR_POW is driver-only but
# only affects matcher-pattern selection at the .cpp level; the PCH itself
# does not see the macro and the constraint still holds.
#
# Disable with -DSTURM_USE_PCH=OFF if PCH ever fights a compiler upgrade.
option(STURM_USE_PCH
    "Enable precompiled headers on the transpiler driver and plugin (\
sturm-cxrn). Default ON cuts whole-build time substantially by parsing \
the clang/llvm umbrella once per build instead of ~50× per TU. Set OFF \
if a compiler/Clang upgrade rejects the PCH (rare)."
    ON)
if(STURM_USE_PCH)
    # Angle-bracket form: CMake passes these through to the compiler as
    # `#include <...>`, resolved via LLVM_INCLUDE_DIRS / CLANG_INCLUDE_DIRS
    # on the target's `-I` line (set above by target_include_directories).
    # Quoted form ("clang/AST/...") would be interpreted as paths relative
    # to ${CMAKE_CURRENT_SOURCE_DIR} and fail to resolve.
    target_precompile_headers(sturm-transpile PRIVATE
        <clang/AST/ASTContext.h>
        <clang/AST/Decl.h>
        <clang/AST/DeclCXX.h>
        <clang/AST/Expr.h>
        <clang/AST/ExprCXX.h>
        <clang/AST/Stmt.h>
        <clang/AST/Type.h>
        <clang/AST/RecursiveASTVisitor.h>
        <clang/AST/ParentMapContext.h>
        <clang/ASTMatchers/ASTMatchers.h>
        <clang/ASTMatchers/ASTMatchFinder.h>
        <clang/Basic/SourceLocation.h>
        <clang/Basic/SourceManager.h>
        <clang/Basic/OperatorKinds.h>
        <clang/Basic/LangOptions.h>
        <clang/Lex/Lexer.h>
        <clang/Rewrite/Core/Rewriter.h>
        <llvm/Support/Casting.h>
        <llvm/ADT/StringRef.h>
        # sturm-cxvo experiment: pull matcher_common.hpp (1087 LOC of inline
        # helpers, included by ~40 matcher TUs at ~2s each) into the PCH so
        # the body is parsed once per build instead of ~40 times. Quoted form
        # so the path is resolved relative to the source dir rather than via
        # -I (the helpers themselves include "sturm/transpile/qir.hpp" which
        # IS on the -I path, so its resolution still works).
        "${CMAKE_CURRENT_SOURCE_DIR}/src/matcher_common.hpp")
    target_precompile_headers(sturm-transpile-plugin REUSE_FROM sturm-transpile)
endif()

# ── Namespaced aliases (sturm-vr0v.1 / E5.M1) ─────────────────────────────────
# Expose the two transpiler targets under their `sturm::` namespace so
# `cmake/SturmTranspile.cmake` can reference them via
# `$<TARGET_FILE:sturm::transpiler>` and `$<TARGET_FILE:sturm::transpile-plugin>`
# uniformly across:
#   - in-tree builds (these aliases),
#   - find_package(sturm) consumers (matching IMPORTED targets registered
#     in cmake/sturmConfig.cmake.in).
# The legacy bare names (`sturm-transpile`, `sturm-transpile-plugin`) remain
# valid for any in-tree call site; the aliases are additive.
add_executable(sturm::transpiler ALIAS sturm-transpile)
add_library(sturm::transpile-plugin ALIAS sturm-transpile-plugin)

# ── Install rule (Phase L PL-1) ───────────────────────────────────────────────
# Ship the transpiler driver into <prefix>/bin so downstream consumers can
# locate it via the IMPORTED `sturm-transpile` target wired up in
# sturmConfig.cmake (see top-level CMakeLists.txt install() block).
install(TARGETS sturm-transpile
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# ── Install rule (Phase M PM1-9) ──────────────────────────────────────────────
# Ship the Clang plugin shared library alongside the driver so downstream
# consumers using the default plugin mode (PM1-5 add_quantum_executable)
# can locate it at <prefix>/lib/libsturm-transpile-plugin.{so,dylib}.
# Linux → libsturm-transpile-plugin.so; macOS → libsturm-transpile-plugin.dylib.
# The target is SHARED (see comment above), so a single LIBRARY DESTINATION
# covers both platforms — CMake routes .so into LIBRARY and .dylib into
# LIBRARY as well (RUNTIME is DLLs on Windows only, which we do not ship).
install(TARGETS sturm-transpile-plugin
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Ship the Clang builtin-headers resource directory alongside the binary so
# the tarball is truly self-contained. At runtime the transpiler resolves
# its resource dir relative to its own executable path
# (`<bindir>/../lib/clang/<LLVM_VERSION_MAJOR>`), so the on-disk layout must
# mirror that. Without these headers, the parser aborts on missing
# `stdarg.h` (every macOS libc sys header needs it) and the output is a
# silent pass-through — exactly the smoke-test failure on a clean runner
# where system LLVM-17 has been purged. See transpiler/src/main.cpp's
# ArgumentsAdjuster for the runtime lookup logic.
if(EXISTS "${STURM_CLANG_RESOURCE_DIR}/include")
    install(DIRECTORY "${STURM_CLANG_RESOURCE_DIR}/include"
            DESTINATION "lib/clang/${LLVM_VERSION_MAJOR}")
else()
    message(WARNING
        "sturm-transpile: Clang builtin-headers dir not found at "
        "${STURM_CLANG_RESOURCE_DIR}/include — installed tarball will not "
        "be self-contained (runs will silently pass through when system "
        "LLVM-17 is absent).")
endif()

# ── sturm-7t85.4 (G4 / PRD A10): sturm_gen path header ────────────────────────
# Generates `${CMAKE_BINARY_DIR}/include/sturm_gen_path.hpp` from
# `transpiler/src/sturm_gen_path.hpp.in`, baking the actual configured
# `${CMAKE_BINARY_DIR}/sturm_gen` directory into a `kSturmGenDir`
# constant. The `test_sturm_gen_clean` ctest (transpiler/tests/) consumes
# this header so the path can never drift between configure-time and
# run-time across the various build trees (build/, build_mac/, build_linux/).
# Risk R7 in the wave-2 PRD: stale path fails at compile, not silently.
set(STURM_GEN_DIR "${CMAKE_BINARY_DIR}/sturm_gen")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sturm_gen_path.hpp.in"
    "${CMAKE_BINARY_DIR}/include/sturm_gen_path.hpp"
    @ONLY)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)
