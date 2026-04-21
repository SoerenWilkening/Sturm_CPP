# tests/transpiler/test_cmake_plugins/CMakeLists.txt — PM4-5 in-tree
# glue fixture for `add_quantum_executable(... PLUGINS <path>)`.
#
# Verifies the PM4-5 extension to the helper from
# `cmake/SturmTranspile.cmake`:
#
#   1. Build-time correctness: the helper accepts an optional `PLUGINS`
#      keyword and, for each path, appends a per-source cc1 arg pair of
#      exactly `-Xclang -plugin-arg-sturm-transpile -Xclang load=<abs-path>`
#      to the source file's COMPILE_OPTIONS. The cc1 spelling is
#      mandatory because the driver splits `-fplugin-arg-` at the first
#      hyphen and the hyphenated plugin name "sturm-transpile" cannot
#      survive the driver-level spelling intact.
#
#   2. Generator-expression plumbing: `PLUGINS $<TARGET_FILE:...>` is
#      supported. The test uses the PM4-7 demo target
#      `sturm-pm4-demo-plugin` (defined in examples/plugin_demo/CMakeLists.txt)
#      as the reference plugin so the genex resolves to a real file
#      path at generate time.
#
#   3. Run-time smoke (test_cmake_plugins_run): compiling the fixture
#      through the helper with a valid plugin attached must still exit 0.
#      The demo plugin is a no-op on non-Sturm TUs — its `register_op`
#      call fires but the matcher never matches anything in the trivial
#      fixture body, so the compile must succeed regardless.
#
# The smoke run is gated on the PM4-7 demo-plugin target being defined.
# In the in-tree monorepo it always is (examples/plugin_demo is always
# subdir'd), but guarding the add_test with a TARGET check keeps the
# fixture resilient to a future refactor that hides the demo behind an
# option flag.

cmake_minimum_required(VERSION 3.16)

# The PM4-7 demo plugin target must be available by generate time for
# the `$<TARGET_FILE:sturm-pm4-demo-plugin>` genex below to resolve.
# examples/ is subdir'd AFTER tests/ in the root CMakeLists.txt, so at
# the time THIS CMakeLists runs the target does not yet exist — but the
# genex is only evaluated at generate time, long after every subdir has
# been processed. We therefore do NOT gate this block on a `TARGET`
# check; if the target genuinely goes missing, CMake's generate step
# will fail with a clear "target not found" error naming the genex.
add_quantum_executable(glue_fixture_plugins
    src/fixture.cpp
    PLUGINS $<TARGET_FILE:sturm-pm4-demo-plugin>)

# Make sure the demo plugin is built before the fixture compiles; its
# MODULE file is what the `load=<path>` arg dlopens at parse time. The
# helper's target-level add_dependencies on `sturm-transpile-plugin`
# takes care of the in-process plugin, but not of downstream PLUGINS
# payloads.
if(TARGET sturm-pm4-demo-plugin)
    add_dependencies(glue_fixture_plugins sturm-pm4-demo-plugin)
endif()

# ── Build-time observation: dump the source's effective COMPILE_OPTIONS ─
# The `set_property(SOURCE ... APPEND PROPERTY COMPILE_OPTIONS ...)` calls
# inside add_quantum_executable produce a list property containing:
#
#   -Xclang ; -plugin-arg-sturm-transpile ; -Xclang ; dump-to=<path> ;
#   -Xclang ; -plugin-arg-sturm-transpile ; -Xclang ; load=<plugin-path>
#
# The order between dump-to= and load= is determined by the foreach-loop
# ordering in SturmTranspile.cmake; the PM4-5 contract does NOT pin the
# order, only that the 4-token load= pair lands contiguously somewhere
# in the list. verify_plugins_flags.cmake scans for a contiguous match.
#
# Dump the property via file(GENERATE), which evaluates any genexes
# (including $<TARGET_FILE:sturm-pm4-demo-plugin>) before writing. The
# fixture source's absolute path is what add_quantum_executable keyed
# the property off of, so we regenerate the same absolute path here.
get_filename_component(_fixture_abs_src
    "${CMAKE_CURRENT_SOURCE_DIR}/src/fixture.cpp" ABSOLUTE)
get_property(_fixture_opts
    SOURCE "${_fixture_abs_src}"
    PROPERTY COMPILE_OPTIONS)
# file(GENERATE ... CONTENT ...) resolves any genexes still in the
# property list at generate time. We join with `\n` so the verify
# script can split line-by-line into argv tokens.
string(REPLACE ";" "\n" _fixture_opts_joined "${_fixture_opts}")
file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/compile_options_observed.txt"
    CONTENT "${_fixture_opts_joined}\n")

# ── Build-time invariants: cc1 arg pair landed on the compile line ────
# The ctest driver reads the `file(GENERATE)` output produced above and
# asserts the 4-token cc1 arg pair
#   -Xclang -plugin-arg-sturm-transpile -Xclang load=<abs-plugin-path>
# is present. The plugin path is taken from the same
# $<TARGET_FILE:...> genex so the expected and observed values stay
# byte-identical; a drift between add_quantum_executable and this test
# means the test fails — which is exactly the PM4-5 contract.
add_test(
    NAME test_cmake_plugins_flag
    COMMAND "${CMAKE_COMMAND}"
            "-DOPTIONS_FILE=${CMAKE_CURRENT_BINARY_DIR}/compile_options_observed.txt"
            "-DPLUGIN_FILE=$<TARGET_FILE:sturm-pm4-demo-plugin>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_plugins_flags.cmake")
set_tests_properties(test_cmake_plugins_flag PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-pm4-demo-plugin>")

# ── Smoke: binary built via PLUGINS arg must still link + exit 0 ──────
# Independent signal from the flag-presence check above: the whole
# compile pipeline runs under the PLUGINS path — plugin loads, the
# transpiler parses the fixture, the nested EmitObjAction succeeds,
# the binary links, and `./glue_fixture_plugins` exits 0. The PM4-8
# smoke will carry over the "plugin actually registered an op" layer
# on top of this; here we just pin that PLUGINS does not break the
# happy-path build.
add_test(NAME test_cmake_plugins_run COMMAND glue_fixture_plugins)
set_tests_properties(test_cmake_plugins_run PROPERTIES
    LABELS "transpiler")
