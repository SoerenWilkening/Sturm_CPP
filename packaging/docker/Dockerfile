# syntax=docker/dockerfile:1.6
#
# Phase L PL-7 — sturm-transpile container image.
#
# Stretch deliverable from the Phase L roadmap: provides a zero-install path
# for users who already run Docker. Published to GHCR by the `docker-publish`
# job in .github/workflows/release.yml on every v* tag.
#
# Design notes
# ------------
# - Two stages. Stage 1 (`build`) installs the full llvm-17 toolchain, builds
#   the transpiler against it, and installs into /opt/sturm. Stage 2 keeps
#   ONLY the LLVM runtime shared libs (libclang-cpp17, libllvm17) that
#   sturm-transpile links against — the dev headers, clang driver, and cmake
#   package files are dropped. This is what keeps the final image under the
#   500 MB acceptance budget.
# - Unlike the release-workflow tarballs, we do NOT bundle dylibs into
#   /opt/sturm/lib and patch RPATH. The runtime stage installs
#   libclang-cpp17/libllvm17 via apt into the image's system lib dir, so
#   the dynamic linker finds them without rpath tricks.
# - ENTRYPOINT is sturm-transpile so `docker run <image> <src.cpp> ...`
#   passes args straight to the transpiler.

# ── Stage 1: build ────────────────────────────────────────────────────────
FROM ubuntu:24.04 AS build

# Bounded build parallelism. Default is 2 so an unattended `docker build`
# on a developer laptop cannot saturate the host with parallel clang
# invocations. CI overrides via `--build-arg BUILD_JOBS=<N>` to match the
# runner's vCPU count (see .github/workflows/release.yml docker-publish).
ARG BUILD_JOBS=2

RUN apt-get update && apt-get install -y --no-install-recommends \
        llvm-17-dev \
        libclang-17-dev \
        clang-17 \
        cmake \
        git \
        ca-certificates \
        build-essential \
    && rm -rf /var/lib/apt/lists/*
# build-essential supplies `make` + a default `cc`/`c++` that CMake's first-run
# compiler detection expects. ubuntu-24.04 *hosted runners* ship it by default,
# so release.yml doesn't install it explicitly — but fresh ubuntu:24.04 base
# images do not, and CMake aborts at "Configuring incomplete" without it.

COPY . /src
WORKDIR /src

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/sturm \
        -DLLVM_DIR=/usr/lib/llvm-17/lib/cmake/llvm \
        -DClang_DIR=/usr/lib/llvm-17/lib/cmake/clang && \
    cmake --build build -j"${BUILD_JOBS}" && \
    cmake --install build

# ── Stage 2: runtime ──────────────────────────────────────────────────────
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
        libclang-cpp17 \
        libllvm17 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /opt/sturm /opt/sturm

# sturm-transpile uses libtooling at runtime to parse source with clang. Two
# header sets must be present in this image for the M7 uncompute-injection
# matcher to see a well-formed AST:
#
#   1. The C++ standard library (<vector>, <cstdint>, ...). On hosted CI
#      runners these come in via the default gcc install; on fresh
#      ubuntu:24.04 they don't. apt-installing libstdc++-13-dev here pulls
#      in the whole gcc-13 toolchain (~120 MB) — over the 500 MB acceptance
#      budget. Copying only the header tree from the build stage adds ~40 MB.
#
#   2. Clang's resource-dir builtin headers (stddef.h, stdint.h, ...).
#      sturm-transpile's InstalledDir is /opt/sturm/bin, so libtooling looks
#      for -resource-dir /opt/sturm/lib/clang/17. The build stage has them
#      under /usr/lib/llvm-17/lib/clang/17/; move them to where the tool
#      expects, so clang stops logging `ignoring nonexistent directory`
#      for /usr/include/clang/17.0.6/include/.
#
# Without (1), sturm-transpile still exits 0 but emits pass-through output
# with no uncompute_*(...) calls injected (parse errors are non-fatal in the
# tool's ClangTool harness).
COPY --from=build /usr/include/c++/13                /usr/include/c++/13
COPY --from=build /usr/include/x86_64-linux-gnu/c++/13 /usr/include/x86_64-linux-gnu/c++/13
COPY --from=build /usr/lib/llvm-17/lib/clang/17/include /opt/sturm/lib/clang/17/include
# (3) crt*.o + libgcc.a from the gcc-13 install. These are what clang's
#     GCCDetection stats to decide "a GCC install exists at
#     /usr/lib/gcc/x86_64-linux-gnu/13" — the side-effect of a successful
#     detection is that the C++ stdlib header paths copied in (1) get added
#     as -internal-isystem entries automatically. Without this, the headers
#     are on disk but never on the include search list.
COPY --from=build /usr/lib/gcc/x86_64-linux-gnu/13 /usr/lib/gcc/x86_64-linux-gnu/13

ENV PATH="/opt/sturm/bin:${PATH}"

ENTRYPOINT ["/opt/sturm/bin/sturm-transpile"]
