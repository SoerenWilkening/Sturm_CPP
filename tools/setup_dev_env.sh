#!/usr/bin/env bash
# tools/setup_dev_env.sh — sturm-q2eb
#
# One-time-per-machine (and per-container) setup that installs the tooling
# the CMake build needs to be fast:
#
#   - ninja        : Ninja generator (>10x faster incremental dependency
#                    scanning than GNU Make on this tree's ~1500 .o files).
#   - ccache       : compiler-output cache (warm-cache full rebuild drops
#                    from ~30 min to a few minutes).
#   - mold (Linux) : drop-in replacement for ld; 2-5x faster link, helps
#                    most on the 9 transpiler-test bucket binaries that
#                    each pull in libclang-cpp at link time.
#
# It also writes ccache's `~/.config/ccache/ccache.conf` with the sloppiness
# settings required for PCH cache hits (clang embeds __DATE__/__TIME__ and
# include-path defines into the PCH; ccache invalidates on every rebuild
# without explicit sloppiness, defeating sturm-bs8s/cxrn's PCH speedup).
#
# Idempotent: rerunning is a fast no-op once everything is installed.
#
# Supports:
#   - Linux (apt-get based; the agent container is ubuntu-derived)
#   - macOS (Homebrew; matches the dev workflow in CLAUDE.md)
#
# Usage:
#   bash tools/setup_dev_env.sh
#
# Exit codes:
#   0   - everything installed / already present
#   1   - install failed
#   2   - unsupported platform

set -euo pipefail

log() { printf '[setup_dev_env] %s\n' "$*" >&2; }

# ── Detect platform ──────────────────────────────────────────────────────────
case "$(uname -s)" in
    Linux)
        PLATFORM=linux
        if ! command -v apt-get >/dev/null 2>&1; then
            log "Unsupported Linux: no apt-get on PATH. Install ccache + ninja-build via your package manager, then rerun configure with -G Ninja."
            exit 2
        fi
        ;;
    Darwin)
        PLATFORM=mac
        if ! command -v brew >/dev/null 2>&1; then
            log "Unsupported macOS: no Homebrew. Install brew (https://brew.sh) or run \`brew install ccache ninja\` by hand."
            exit 2
        fi
        ;;
    *)
        log "Unsupported uname: $(uname -s). Install ccache + ninja by hand."
        exit 2
        ;;
esac

# ── Install ccache + ninja + mold (Linux only) ───────────────────────────────
# mold is Linux-only — Apple's ld64 already outperforms gold/ld on Mach-O,
# and the STURM build does not currently ship a Mac-equivalent fast linker
# story. The CMake gate (STURM_USE_MOLD) auto-skips on Darwin.
have_ccache=0; have_ninja=0; have_mold=0
command -v ccache >/dev/null 2>&1 && have_ccache=1
command -v ninja  >/dev/null 2>&1 && have_ninja=1
if [[ "$PLATFORM" == "linux" ]]; then
    command -v mold >/dev/null 2>&1 && have_mold=1
else
    have_mold=1  # vacuously "satisfied" on macOS — gate not exercised
fi

if [[ $have_ccache -eq 1 && $have_ninja -eq 1 && $have_mold -eq 1 ]]; then
    log "All build tooling present ($(ccache --version | head -1); ninja $(ninja --version)$([[ "$PLATFORM" == "linux" ]] && echo "; $(mold --version 2>&1 | head -1)" || echo ""))"
else
    log "Installing missing tooling (ccache=$have_ccache, ninja=$have_ninja, mold=$have_mold) for $PLATFORM..."
    case "$PLATFORM" in
        linux)
            if [[ $EUID -ne 0 ]] && command -v sudo >/dev/null 2>&1; then
                SUDO=sudo
            else
                SUDO=
            fi
            $SUDO apt-get update -qq
            $SUDO apt-get install -y --no-install-recommends ccache ninja-build mold
            ;;
        mac)
            packages=()
            [[ $have_ccache -eq 0 ]] && packages+=(ccache)
            [[ $have_ninja  -eq 0 ]] && packages+=(ninja)
            brew install "${packages[@]}"
            ;;
    esac
fi

# ── Configure ccache (idempotent: --set-config overwrites a single key) ──────
# Why these knobs:
#   max_size=5G                : the transpiler's libclang-cpp-heavy .o files
#                                are 100-400 KB each; 1500 of them × maybe 4
#                                variants is ~2 GB. 5 GB leaves headroom for
#                                Release/Debug toggles without thrashing.
#   sloppiness=                : ccache invalidates a cache entry whenever it
#                                cannot prove byte-equivalence of the inputs.
#                                The keys below tell ccache that the listed
#                                discrepancies are benign for STURM's PCH
#                                pipeline (sturm-bs8s/cxrn):
#     pch_defines              : do not invalidate on changed -D values that
#                                are embedded in the PCH preamble.
#     time_macros              : ignore __DATE__/__TIME__ macros, which the
#                                PCH emits even though no source uses them.
#     include_file_mtime       : ignore header mtime; ccache still hashes the
#                                file content, so this only skips a redundant
#                                stat-based shortcut.
#     include_file_ctime       : same rationale on ctime.
log "Configuring ccache: max_size=5G + PCH-safe sloppiness + depend_mode"
ccache --max-size 5G
ccache --set-config sloppiness=pch_defines,time_macros,include_file_mtime,include_file_ctime
# depend_mode: skip the preprocessor pass on cache lookup and hash the compiler-emitted
# dependency file (-MD) instead. STURM already compiles with -MD/-MF for every TU
# (Ninja generator's default), so the dep file is on disk; depend_mode just reuses it.
# Saves 30-60% per-compile wall on heavy template TUs (libclang AST headers etc.)
# at the cost of slightly weaker cache invalidation — only matters when the user
# rewrites a header without changing the source's recorded dependency list.
ccache --set-config depend_mode=true
# compiler_check=content: ccache normally identifies the compiler by mtime,
# which silently invalidates the entire cache the next time `apt upgrade`
# touches /usr/lib/llvm-17/bin/clang++. Content-hashing the compiler binary
# survives those touches at the cost of one stat+hash per compile (~ms).
ccache --set-config compiler_check=content

# ── Report ───────────────────────────────────────────────────────────────────
log "ccache config:"
ccache --show-config | grep -E '(max_size|sloppiness|cache_dir)\s*=' | sed 's/^/    /'
log "Versions: $(ccache --version | head -1); ninja $(ninja --version)"
log "Done. Recommended next step:"
log "    rm -rf build && cmake -S . -B build -G Ninja \\"
log "        -DCMAKE_BUILD_TYPE=Release \\"
case "$PLATFORM" in
    linux)
        log "        -DCMAKE_CXX_COMPILER=/usr/lib/llvm-17/bin/clang++ \\"
        log "        -DLLVM_DIR=/usr/lib/llvm-17/lib/cmake/llvm \\"
        log "        -DClang_DIR=/usr/lib/llvm-17/lib/cmake/clang"
        ;;
    mac)
        log "        -DCMAKE_CXX_COMPILER=/usr/local/opt/llvm@17/bin/clang++ \\"
        log "        -DLLVM_DIR=/usr/local/opt/llvm@17/lib/cmake/llvm \\"
        log "        -DClang_DIR=/usr/local/opt/llvm@17/lib/cmake/clang"
        ;;
esac
log "Then: cmake --build build --parallel 6"
