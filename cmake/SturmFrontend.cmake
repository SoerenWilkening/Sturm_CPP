# SturmFrontend.cmake — sturm-mixe / Frontend simpl. P1 (Plan §3 P1, PRD §5.3, G3).
#
# Declares the `STURM_MODE` cache STRING (default APPEND, allowed values
# APPEND|COUNT|SIMULATE), validates it, and exposes the `sturm::frontend`
# INTERFACE library carrying `STURM_MODE_DEFAULT=STURM_MODE_<X>` as a
# compile definition. Toggling `-DSTURM_MODE=...` between configures
# re-runs this file, the new value lands on the INTERFACE lib's
# `INTERFACE_COMPILE_DEFINITIONS`, and CMake's dependency walker invalidates
# every downstream TU that links to `sturm::frontend` — this is OQ5's
# rebuild semantics, pinned by the `cmake_sturm_mode_rebuild` ctest.
#
# Phase note (sturm-mixe): the macro has no run-time consumers yet —
# Phase 7 (`sturm-iyrr`, matcher_main_lifecycle) wires it through the
# transpiler's auto-injected `sturm_backend_create` call. The
# `sturm_backend_create` C ABI still takes `(mode)` at this phase.

# ── 1. Cache variable + allowed-values metadata ────────────────────────────
set(STURM_MODE "APPEND" CACHE STRING
    "Default execution mode for the sturm frontend (sturm-mixe / PRD §5.3): APPEND records gates into an in-memory IR buffer; COUNT counts gates only (no IR, no state); SIMULATE drives Orkan's statevector backend. The chosen value is propagated to TUs as STURM_MODE_DEFAULT=STURM_MODE_<X> via the sturm::frontend INTERFACE library; toggling between configures rebuilds dependents (OQ5).")

# Make the cache value enumerable in `cmake-gui` / `ccmake`. The list
# below is also the validation source of truth — if you add a value here,
# extend the if/elseif chain below to map it. CMake 4.3+ tightened
# `set_property`'s argument parser so passing each token as a separate
# arg trips a spurious "invalid argument" diagnostic on the second
# token; passing the list as one semicolon-joined string sidesteps that.
set_property(CACHE STURM_MODE PROPERTY STRINGS "APPEND;COUNT;SIMULATE")

# ── 2. Configure-time validation ───────────────────────────────────────────
# Map the user-facing token to the C-ABI enum identifier from
# `include/sturm/core/core.h`. APPEND/COUNT/SIMULATE are user-friendly;
# the C enum values are STURM_MODE_APPEND / STURM_MODE_COUNT_ONLY /
# STURM_MODE_SIMULATE. An invalid token FATAL_ERRORs with an actionable
# diagnostic naming the offending value AND the allowed set, pinned by
# the `cmake_sturm_mode_flag` ctest.
if(STURM_MODE STREQUAL "APPEND")
    set(_sturm_mode_default_macro "STURM_MODE_APPEND")
elseif(STURM_MODE STREQUAL "COUNT")
    set(_sturm_mode_default_macro "STURM_MODE_COUNT_ONLY")
elseif(STURM_MODE STREQUAL "SIMULATE")
    set(_sturm_mode_default_macro "STURM_MODE_SIMULATE")
else()
    message(FATAL_ERROR
        "sturm-mixe: invalid STURM_MODE='${STURM_MODE}'. "
        "Allowed values: APPEND (default), COUNT, SIMULATE. "
        "Set with `-DSTURM_MODE=APPEND|COUNT|SIMULATE` at configure time. "
        "See docs/prd_frontend_simplification.md §5.3 (G3) for semantics.")
endif()

message(STATUS "sturm: STURM_MODE=${STURM_MODE} "
               "(STURM_MODE_DEFAULT=${_sturm_mode_default_macro})")

# ── 3. INTERFACE library carrying STURM_MODE_DEFAULT ───────────────────────
# `sturm::frontend` is the consumer-facing front door for the auto-injected
# lifecycle macros. It carries no headers/sources of its own at this phase
# (P3 wires the umbrella; P7 wires the auto-injection). The compile
# definition lives on INTERFACE_COMPILE_DEFINITIONS so any target that
# `target_link_libraries(... PRIVATE/PUBLIC sturm::frontend)` sees
# `-DSTURM_MODE_DEFAULT=STURM_MODE_<X>` on its compile line, and changing
# the value re-runs CMake (cache var update) which invalidates dependents.
if(NOT TARGET sturm_frontend)
    add_library(sturm_frontend INTERFACE)
    add_library(sturm::frontend ALIAS sturm_frontend)
endif()
target_compile_definitions(sturm_frontend
    INTERFACE "STURM_MODE_DEFAULT=${_sturm_mode_default_macro}")
