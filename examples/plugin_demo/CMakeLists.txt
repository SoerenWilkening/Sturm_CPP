# examples/plugin_demo/CMakeLists.txt — PM4-7 demo plugin.
#
# Builds `sturm-pm4-demo-plugin` as a **MODULE** shared library (per the
# PM4-7 issue spec and the plan §5 discipline: runtime-load plugins are
# MODULE artifacts, while the in-tree clang plugin `sturm-transpile-plugin`
# is SHARED because the transpiler itself is what loads it and wires into
# Clang's FrontendPluginRegistry).
#
# Scope reminder (see examples/plugin_demo/plugin_demo.cpp for the full
# rationale): the demo exercises the Registry API — specifically
# `register_op(kind_id="pm4.demo.tag", matcher_fn=no-op, render_fn)` —
# end-to-end via the `sturm_register_plugin_v1` entry point. It is NOT a
# dogfood migration (see PM4-6) and is NOT wired into any downstream
# `add_quantum_executable` target. The PM4-8 and PM4-10 smoke tests are
# the consumers of this target; the PM4-7 unit test itself links the
# plugin's TU directly.
#
# CMake target layout
# -------------------
# * `sturm-pm4-demo-plugin` — MODULE library containing the plugin's
#   single TU (`plugin_demo.cpp`). MODULE maps to `.so` on Linux and
#   `.dylib` on macOS; either is dlopen-compatible. The platform-specific
#   prefix (`lib`) and file extension are the default CMake choices for
#   MODULE libraries — no PREFIX / SUFFIX override is needed because the
#   host's `dlopen(path, ...)` takes the absolute path via the
#   `$<TARGET_FILE:sturm-pm4-demo-plugin>` generator expression.
#
# Include / link discipline
# -------------------------
# The plugin_api.hpp header is under `transpiler/include/sturm/transpile/`
# — it is the sole public surface a third-party plugin TU includes. The
# `Registry` methods referenced from `sturm_register_plugin_v1` resolve
# via the host `sturm-transpile-plugin.so` (which embeds
# `plugin_registry.cpp`) once the demo plugin is dlopen'd with
# `RTLD_LOCAL | RTLD_NOW`: the host's Registry symbols are visible on the
# global lookup scope (RTLD_GLOBAL side) so the plugin's undefined
# references resolve against them. For the PM4-7 unit test harness we
# compile plugin_demo.cpp + plugin_registry.cpp + qir.cpp into a single
# test binary so the same symbols resolve at link time — no dlopen
# involved. This mirrors the shape used by test_plugin_registry (see
# transpiler/tests/CMakeLists.txt:106-143).
#
# `plugin_api.hpp` forward-declares `clang::ast_matchers::MatchFinder`
# so the demo plugin can stay off the heavy ASTMatchFinder.h include —
# the plugin body never dereferences `MatchFinder&`, it only forwards
# the reference into the no-op lambda's signature. This keeps the demo
# compile fast (no Clang AST headers dragged in just to build the
# plugin) and exercises the header's "forward-declare MatchFinder"
# contract.

cmake_minimum_required(VERSION 3.16)

# ── LLVM / Clang dependency resolution ─────────────────────────────────────
# The top-level CMakeLists.txt scopes `find_package(LLVM CONFIG)` inside the
# transpiler/ subdirectory, so `LLVM_INCLUDE_DIRS` / `CLANG_INCLUDE_DIRS` /
# `LLVM_DEFINITIONS` are not visible here by default. Re-run the lookup
# locally so the plugin's compile can resolve the `clang/Basic/
# SourceLocation.h` include pulled in transitively through
# `sturm/transpile/qir.hpp`. The `find_package` call is cheap (the LLVM
# config files are already cached by the earlier transpiler invocation)
# and keeps the plugin target self-contained — no reliance on parent-scope
# variables.
find_package(LLVM CONFIG REQUIRED)
find_package(Clang CONFIG REQUIRED)

# MODULE keyword: dlopen-only shared library. The host never calls
# `target_link_libraries(<consumer> sturm-pm4-demo-plugin)`; the only
# way this `.so` / `.dylib` reaches the transpile pipeline is through
# runtime dlopen (PM4-4) or via link-time bake-in under the
# `STURM_PM4_LINK_DEMO=ON` CMake option (PM4-10).
add_library(sturm-pm4-demo-plugin MODULE
    plugin_demo.cpp
)

# The plugin TU includes `sturm/transpile/plugin_api.hpp`, which in turn
# pulls `sturm/transpile/qir.hpp`. Both live under
# `transpiler/include/`. Clang's `MatchFinder` is forward-declared in
# plugin_api.hpp so no Clang AST headers are needed here — but the
# forward declaration STILL needs the `clang/ASTMatchers/ASTMatchFinder.h`
# include path to be available IF we later add a matcher body; for now
# we add LLVM/Clang include dirs to keep the include chain consistent
# with every other transpiler-adjacent target in the tree, so that if a
# future PM4-X demo grows a real matcher it does not need a fresh CMake
# plumbing pass.
target_include_directories(sturm-pm4-demo-plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/transpiler/include
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)

# Match the main transpiler's RTTI / LLVM definitions posture so the
# Clang-adjacent headers (pulled in transitively via qir.hpp ->
# SourceLocation.h) see the same configuration the host was compiled
# with. `LLVM_DEFINITIONS` / `LLVM_ENABLE_RTTI` are defined in the
# transpiler subdirectory via `find_package(LLVM CONFIG)`; we rely on
# that discovery already having run (examples/ is entered after
# transpiler/ in the top-level CMakeLists.txt).
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(sturm-pm4-demo-plugin PRIVATE -fno-rtti)
endif()
target_compile_features(sturm-pm4-demo-plugin PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_PM4_DEMO NATIVE_COMMAND
    "${LLVM_DEFINITIONS}")
target_compile_definitions(sturm-pm4-demo-plugin PRIVATE
    ${LLVM_DEFS_LIST_PM4_DEMO})

# No target_link_libraries against clang / LLVM. The plugin body does
# not call into any Clang symbol (the MatchFinder parameter of the
# no-op lambda is passed through by reference and never dereferenced),
# so the `.so` / `.dylib` carries zero LLVM DT_NEEDED entries. At
# dlopen time the plugin's only undefined references are against the
# host's `plugin_registry.cpp` symbols (Registry::register_op), which
# resolve against the already-loaded host process image under
# RTLD_LOCAL | RTLD_NOW.
#
# The Registry method bodies live in the host (sturm-transpile or
# sturm-transpile-plugin). On Linux we need to let the linker know
# these symbols are allowed to be undefined in the MODULE image —
# default GNU ld behavior for SHARED / MODULE libraries already
# permits undefined references (-shared implies --unresolved-symbols=
# ignore-in-shared-libs), so no extra flag is needed. On macOS the
# analogous default (-undefined dynamic_lookup) is what we want; we
# enable it explicitly via the CMake target property
# ENABLE_EXPORTS + MACOSX_RPATH to keep the behavior predictable.
set_target_properties(sturm-pm4-demo-plugin PROPERTIES
    # Predictable filename: `libsturm-pm4-demo-plugin.so` on Linux,
    # `libsturm-pm4-demo-plugin.dylib` on macOS. The default MODULE
    # prefix is `lib` on both platforms; we pin it so the smoke-test
    # glob / TARGET_FILE generator expression stays stable.
    PREFIX "lib"
)
if(APPLE)
    # Allow undefined references into the host's symbol table at
    # dlopen time. Without this flag, `Registry::register_op` (defined
    # in the host's plugin_registry.cpp) is unresolved at link time and
    # the MODULE build fails.
    target_link_options(sturm-pm4-demo-plugin PRIVATE
        "LINKER:-undefined,dynamic_lookup")
endif()
