# check_example_qram_demo_run.cmake — sturm-7t85.5 (Beat G5) run-check.
#
# Plan §19f, PRD A9. End-to-end run gate for examples/qram_demo.cpp:
# the example must build (handled by the FIXTURES_REQUIRED setup),
# launch with exit code 0, and emit a non-empty ASCII circuit diagram on
# stdout (the `--- QRAM read circuit (APPEND-mode IR) ---` header
# delimits the diagram region).
#
# Invoked by tests/transpiler/CMakeLists.txt with:
#   -DEXAMPLE_BIN=<abs path to example_qram_demo binary>
#
# Contract:
#   1. EXAMPLE_BIN exists (the build_example_qram_demo SETUP fixture
#      should have produced it; if it did not, fail loudly here).
#   2. Running the binary returns exit code 0.
#   3. Captured stdout contains the diagram-header line
#      `--- QRAM read circuit (APPEND-mode IR) ---`.
#   4. At least one drawn-row line (matches `^q[0-9]+:`) appears AFTER
#      the header — proves the diagram region is non-empty (the
#      sturm::draw_ascii output is a sequence of `qN: ...` rows).
#   5. The trailing `[gate count = N]` line is present and N is > 0
#      (a zero gate count would mean the QRAM read produced no IR,
#      which would silently hide an upstream regression).
#
# The script exits 0 on success, 1 on any failure.

if(NOT DEFINED EXAMPLE_BIN)
    message(FATAL_ERROR
        "check_example_qram_demo_run: EXAMPLE_BIN is not set. "
        "Pass -DEXAMPLE_BIN=$<TARGET_FILE:example_qram_demo>.")
endif()

if(NOT EXISTS "${EXAMPLE_BIN}")
    message(FATAL_ERROR
        "check_example_qram_demo_run: example binary missing: "
        "${EXAMPLE_BIN}\n"
        "The build_example_qram_demo SETUP fixture should have built it "
        "before this check runs.")
endif()

# ── Assertion 2: launch the binary; exit code must be 0 ─────────────────────
execute_process(
    COMMAND "${EXAMPLE_BIN}"
    RESULT_VARIABLE _qram_rc
    OUTPUT_VARIABLE _qram_stdout
    ERROR_VARIABLE  _qram_stderr
)
if(NOT _qram_rc EQUAL 0)
    message(FATAL_ERROR
        "check_example_qram_demo_run: example_qram_demo exited with "
        "non-zero status ${_qram_rc}.\n"
        "  binary: ${EXAMPLE_BIN}\n"
        "  stdout:\n${_qram_stdout}\n"
        "  stderr:\n${_qram_stderr}")
endif()

# ── Assertion 3: diagram header is present ──────────────────────────────────
# The example prints `\n--- QRAM read circuit (APPEND-mode IR) ---\n`
# immediately before the rendered diagram (qram_demo.cpp:70).
string(FIND "${_qram_stdout}" "--- QRAM read circuit (APPEND-mode IR) ---"
    _hdr_pos)
if(_hdr_pos EQUAL -1)
    message(FATAL_ERROR
        "check_example_qram_demo_run: missing diagram header in stdout.\n"
        "Expected to find the substring "
        "`--- QRAM read circuit (APPEND-mode IR) ---`.\n"
        "  stdout:\n${_qram_stdout}")
endif()

# ── Assertion 4: at least one drawn `qN:` row in the diagram ────────────────
# `sturm::draw_ascii` lays out the circuit as one row per qubit, each
# prefixed `q<N>: `. A diagram with zero rows would mean the renderer
# returned an empty string — that should never happen for a non-empty
# IR, so guard against it explicitly.
string(REGEX MATCHALL "(^|\n)q[0-9]+:" _row_hits "${_qram_stdout}")
list(LENGTH _row_hits _row_count)
if(_row_count LESS 1)
    message(FATAL_ERROR
        "check_example_qram_demo_run: diagram is empty (no `qN:` rows).\n"
        "Expected at least one `q<index>:` line in the rendered "
        "circuit.\n"
        "  stdout:\n${_qram_stdout}")
endif()

# ── Assertion 5: gate-count line exists with a strictly positive count ──────
# The example prints `\n[gate count = %zu]\n` (qram_demo.cpp:72) right
# after the diagram. A gate count of 0 would mean the QRAM read emitted
# no IR — a silent regression we explicitly reject.
string(REGEX MATCH "\\[gate count = ([0-9]+)\\]" _gc_match "${_qram_stdout}")
if(NOT _gc_match)
    message(FATAL_ERROR
        "check_example_qram_demo_run: missing `[gate count = N]` line "
        "in stdout.\n"
        "  stdout:\n${_qram_stdout}")
endif()
set(_gate_count "${CMAKE_MATCH_1}")
if(_gate_count EQUAL 0)
    message(FATAL_ERROR
        "check_example_qram_demo_run: gate count is 0 — QRAM read "
        "produced no IR. This is a silent upstream regression.\n"
        "  stdout:\n${_qram_stdout}")
endif()

message(STATUS
    "check_example_qram_demo_run: OK — exit 0, header present, "
    "${_row_count} `qN:` row(s), gate count = ${_gate_count}")
