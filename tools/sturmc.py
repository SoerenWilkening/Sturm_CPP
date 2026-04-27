#!/usr/bin/env python3
"""sturmc — STURM one-shot transpile + compile + link driver.

PRD §3.5 / plan §E5.M2. Takes a single .cpp, runs `sturm-transpile` to
rewrite it, then invokes ${CXX} on the rewritten source against the
installed STURM prefix. Layout assumed:

    <prefix>/bin/sturmc            ← this script
    <prefix>/bin/sturm-transpile
    <prefix>/include/sturm/...
    <prefix>/lib/libsturm.{a,so,dylib}   (linked iff --link sturm AND
                                          the file exists; absent today
                                          per D3 — header-only surface)

Prefix is resolved as `parent(parent(realpath(__file__)))` so the
install tree is fully relocatable. Stdlib only. LOC budget ≤ 200.
"""
from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def _resolve_prefix() -> Path:
    """Compute the install prefix from the script's own location.

    sturmc lives at <prefix>/bin/sturmc; the realpath collapses any symlinks
    a packager might have planted (Homebrew's keg-relocation does this).
    """
    here = Path(os.path.realpath(__file__)).resolve()
    return here.parent.parent


def _find_transpiler(prefix: Path) -> Path:
    cand = prefix / "bin" / "sturm-transpile"
    if not cand.is_file():
        sys.stderr.write(
            f"sturmc: cannot find sturm-transpile at {cand}. "
            "Is the STURM package fully installed under "
            f"prefix={prefix}?\n"
        )
        sys.exit(2)
    return cand


def _find_libsturm(prefix: Path) -> Path | None:
    """Locate libsturm.{a,so,dylib} under <prefix>/lib if it has been
    shipped. Today the runtime is not yet packaged (PRD §3.3 / D3 — the
    public surface is header-only by necessity), so we treat absence as
    a forward-compatible no-op rather than failing the build."""
    libdir = prefix / "lib"
    sysname = platform.system()
    if sysname == "Darwin":
        names = ("libsturm.dylib", "libsturm.a")
    elif sysname == "Windows":
        names = ("sturm.lib", "libsturm.a")
    else:
        names = ("libsturm.so", "libsturm.a")
    for name in names:
        p = libdir / name
        if p.is_file():
            return p
    return None


def _parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        prog="sturmc",
        description=(
            "Transpile a STURM .cpp source through sturm-transpile, then "
            "compile + link the rewritten output via ${CXX}."
        ),
    )
    p.add_argument("input", type=Path, help="Input .cpp source.")
    p.add_argument("-o", "--output", type=Path, required=True,
                   help="Output binary path.")
    p.add_argument("--cxx", default=os.environ.get("CXX", "c++"),
                   help="C++ compiler (default: $CXX or c++).")
    p.add_argument("--include-dirs", action="append", default=[],
                   help="Extra include directory; pass multiple times "
                        "(or comma-separated) to add several.")
    p.add_argument("--link", action="append", default=[],
                   metavar="LIB",
                   help="Library to link (`--link sturm` resolves to "
                        "<prefix>/lib/libsturm.* when present).")
    p.add_argument("--keep-temp", action="store_true",
                   help="Do not delete the transpile workspace on exit.")
    p.add_argument("-v", "--verbose", action="store_true",
                   help="Echo each subprocess command line before running.")
    return p.parse_args(argv)


def _run(cmd: list[str], *, verbose: bool) -> None:
    if verbose:
        sys.stderr.write("sturmc: + " + " ".join(cmd) + "\n")
    rc = subprocess.call(cmd)
    if rc != 0:
        sys.stderr.write(
            f"sturmc: command failed (rc={rc}): {' '.join(cmd)}\n"
        )
        sys.exit(rc)


def _flatten_includes(values: list[str]) -> list[str]:
    out: list[str] = []
    for v in values or []:
        if not v:
            continue
        for piece in v.split(","):
            piece = piece.strip()
            if piece:
                out.append(piece)
    return out


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv if argv is not None else sys.argv[1:])

    prefix = _resolve_prefix()
    transpiler = _find_transpiler(prefix)
    public_inc = prefix / "include"

    if not args.input.is_file():
        sys.stderr.write(f"sturmc: input not found: {args.input}\n")
        return 2

    cxx = shutil.which(args.cxx) or args.cxx  # best-effort resolution

    workdir_obj = tempfile.TemporaryDirectory(prefix="sturmc-")
    workdir = Path(workdir_obj.name)
    if args.keep_temp:
        workdir_obj._finalizer.detach()  # type: ignore[attr-defined]
        sys.stderr.write(f"sturmc: keeping temp workspace at {workdir}\n")

    # Step 1: transpile. We use --dump-transpiled so the rewritten buffer
    # lands at a known path regardless of the input's relative-path shape.
    transpiled = workdir / (args.input.stem + ".transpiled.cpp")
    # Mirror the contract `add_quantum_executable` imposes on every
    # consumer (cmake/SturmTranspile.cmake §plugin-mode):
    #   * `STURM_BACKEND_ENABLED=1` — the public umbrella
    #     `<sturm/sturm.hpp>` unconditionally pulls in
    #     `qtypes/qint_arith_v3.hpp`, which `#error`s when the macro is
    #     unset.
    #   * `STURM_ANCILLA_CAPACITY=256` — `core/qubit_pool.hpp` uses it
    #     as an array bound. Matches the parent CMakeLists.txt default.
    # Setting both in the transpile parse and the compile step keeps
    # sturmc consumable for arbitrary STURM sources (PRD §3.5 / D3).
    transpile_cmd = [
        str(transpiler),
        str(args.input.resolve()),
        "--dump-transpiled", str(transpiled),
        f"--extra-arg=-I{public_inc}",
        "--extra-arg=-std=c++20",
        "--extra-arg=-DSTURM_BACKEND_ENABLED=1",
        "--extra-arg=-DSTURM_ANCILLA_CAPACITY=256",
    ]
    for inc in _flatten_includes(args.include_dirs):
        transpile_cmd.append(f"--extra-arg=-I{inc}")
    _run(transpile_cmd, verbose=args.verbose)

    if not transpiled.is_file():
        sys.stderr.write(
            f"sturmc: transpiler did not produce {transpiled}\n"
        )
        return 1

    # Step 2: compile + link via ${CXX}. Same `STURM_BACKEND_ENABLED` /
    # `STURM_ANCILLA_CAPACITY` rationale as the transpile parse above.
    compile_cmd: list[str] = [cxx, "-std=c++20",
                              "-DSTURM_BACKEND_ENABLED=1",
                              "-DSTURM_ANCILLA_CAPACITY=256",
                              str(transpiled), f"-I{public_inc}"]
    for inc in _flatten_includes(args.include_dirs):
        compile_cmd.append(f"-I{inc}")

    libdir_added = False
    for lib in args.link:
        if lib == "sturm":
            libfile = _find_libsturm(prefix)
            if libfile is not None:
                if not libdir_added:
                    compile_cmd.append(f"-L{prefix / 'lib'}")
                    libdir_added = True
                compile_cmd.append("-lsturm")
            elif args.verbose:
                sys.stderr.write(
                    "sturmc: --link sturm requested but no libsturm "
                    f"found under {prefix / 'lib'}; treating as a "
                    "header-only build (forward-compatible no-op).\n"
                )
        else:
            compile_cmd.append(f"-l{lib}")

    compile_cmd += ["-o", str(args.output)]
    _run(compile_cmd, verbose=args.verbose)
    return 0


if __name__ == "__main__":
    sys.exit(main())
