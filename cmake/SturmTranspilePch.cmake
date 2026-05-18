# cmake/SturmTranspilePch.cmake — sturm-bs8s helper for opting consumer
# targets (transpiler test executables) into the `sturm-transpile` PCH.
#
# Background (sturm-bs8s)
# -----------------------
# `transpiler/CMakeLists.txt` builds a precompiled header on
# `sturm-transpile` covering the ~20 clang/AST + clang/ASTMatchers +
# clang/Basic / clang/Lex / clang/Rewrite / llvm/Support / llvm/ADT
# headers that every matcher / emitter / consumer TU drags in. The
# `sturm-transpile-plugin` SHARED library `REUSE_FROM`s it (see
# `transpiler/CMakeLists.txt:780`). Each transpiler test target compiles
# its own copies of the same heavy headers — a measured ~12 s per
# heavy TU on Linux clang++-17. PCH reuse cuts that to ~4.4 s
# (~64% reduction).
#
# REUSE_FROM contract (constraints inherited from sturm-2vbr)
# -----------------------------------------------------------
# Clang refuses to read a PCH from a consumer compiled in a different
# PIE state ("is pie differs in PCH file vs. current file"). The PCH
# producer (`sturm-transpile`) is pinned to `-fPIC` via
# `target_compile_options(sturm-transpile PRIVATE -fPIC)`
# (transpiler/CMakeLists.txt:294 — same -fPIC the plugin consumes).
# Every consumer of the PCH must therefore also compile with `-fPIC`.
# CMake's `target_precompile_headers(... REUSE_FROM ...)` enforces a
# matching `target_compile_options` list across both targets, but it
# does NOT normalize default PIE state per target type. Adding `-fPIC`
# explicitly to each consumer keeps the two sides in agreement.
#
# Public surface
# --------------
#   sturm_use_transpile_pch(<target>)
#     - Adds `-fPIC` to <target> as a PRIVATE compile option (no-op on
#       macOS, where Mach-O forces PIC; required on Linux executables
#       which default to `-fpie`).
#     - Calls `target_precompile_headers(<target> REUSE_FROM
#       sturm-transpile)`.
#     - No-op when `STURM_USE_PCH` is OFF (mirrors the gate
#       transpiler/CMakeLists.txt uses to wire the PCH on / off).
#     - No-op (with a one-line `message(STATUS ...)`) when the
#       `sturm-transpile` target is not yet defined — defensive guard so
#       a downstream consumer that includes this file before the
#       transpiler subdir has been added does not hard-fail.
#
# Idempotency
# -----------
# Include-guard via `_STURM_TRANSPILE_PCH_CMAKE_INCLUDED` so multiple
# `include()` calls in the same configure are harmless.

if(DEFINED _STURM_TRANSPILE_PCH_CMAKE_INCLUDED)
    return()
endif()
set(_STURM_TRANSPILE_PCH_CMAKE_INCLUDED ON)

# ── Public helper ─────────────────────────────────────────────────────────
function(sturm_use_transpile_pch _target)
    if(NOT TARGET ${_target})
        message(FATAL_ERROR
            "sturm_use_transpile_pch: target '${_target}' is not defined. "
            "Call this helper AFTER add_executable / add_library that "
            "creates the target.")
    endif()

    # Honour the global PCH toggle. When STURM_USE_PCH=OFF the
    # sturm-transpile target carries no PCH directives, so REUSE_FROM
    # would attach an empty PCH; staying silent here avoids confusing
    # build output.
    if(DEFINED STURM_USE_PCH AND NOT STURM_USE_PCH)
        return()
    endif()

    if(NOT TARGET sturm-transpile)
        message(STATUS
            "sturm_use_transpile_pch(${_target}): skipping — "
            "sturm-transpile target is not yet defined. Ensure the "
            "transpiler subdir has been added before this helper fires.")
        return()
    endif()

    # PIE-state match: the sturm-transpile PCH is built with `-fPIC`
    # (sturm-2vbr). Any consumer reading it must also compile with
    # `-fPIC` or clang rejects the PCH with `is pie differs in PCH file
    # vs. current file`. Adding `-fPIC` PRIVATE composes cleanly with
    # any other compile options the caller may have set; on macOS the
    # flag is a no-op (Mach-O forces PIC unconditionally).
    target_compile_options(${_target} PRIVATE -fPIC)

    # Attach the REUSE_FROM directive. CMake's PCH consistency check
    # will compare the consumer's `target_compile_options`,
    # `target_compile_definitions`, and `target_compile_features`
    # against the producer's at generate time; the transpiler test
    # targets that this helper is called on already match the producer
    # on `cxx_std_20` + `LLVM_DEFINITIONS` so no additional plumbing is
    # required.
    target_precompile_headers(${_target} REUSE_FROM sturm-transpile)
endfunction()
