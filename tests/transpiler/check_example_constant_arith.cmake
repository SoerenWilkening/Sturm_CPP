# check_example_constant_arith.cmake — Phase B example-observability ctest.
#
# Invoked by tests/transpiler/CMakeLists.txt with:
#   -DSOURCE=<abs path>    — the source example examples/constant_arith.cpp
#   -DGENERATED=<abs path> — the transpiler-generated sibling under
#                            <CMAKE_BINARY_DIR>/sturm_gen/examples/
#                            constant_arith.cpp
#
# Contract:
#   After a clean build that routes examples/constant_arith.cpp through
#   sturm-transpile, the generated file must contain the four injected
#   inverses for the Phase B compound-assign operators, AND the source
#   file must be untouched (the transpiler never rewrites its input in
#   place — it only writes into sturm_gen/).
#
# Assertions:
#   1. GENERATED exists on disk.
#   2. GENERATED contains each of the four injected inverse statements:
#        a *= 3;   (from /= 3)
#        a /= 3;   (from *= 3)
#        a += 3;   (from -= 3)
#        a -= 3;   (from += 3)
#      The regexes tolerate internal whitespace. Each inverse is checked
#      for a count of >= 1 (exactly-one would over-specify — the source
#      also contains a forward `a -= 3;` etc., so the corresponding
#      inverse pushes the total to 2; we require *at least* the pair).
#   3. The four injected inverses appear in LIFO (reverse source) order
#      inside the generated file. The forward chain is `+=, -=, *=, /=`,
#      so the injected block must be `*=, /=, +=, -=` — with each match
#      strictly after the last forward statement.
#   4. SOURCE does NOT contain the `a *= 3;` injected signature (the
#      forward source uses `*=` only once, and that occurrence lives
#      ABOVE the injection point; see the `forward-only-has-one-of-each`
#      structural guard below). The check mirrors the generated-file
#      regex (same whitespace tolerance).

if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "check_example_constant_arith: SOURCE not set")
endif()
if(NOT DEFINED GENERATED)
    message(FATAL_ERROR "check_example_constant_arith: GENERATED not set")
endif()

if(NOT EXISTS "${GENERATED}")
    message(FATAL_ERROR
        "check_example_constant_arith: generated file missing: ${GENERATED}")
endif()
if(NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR
        "check_example_constant_arith: source file missing: ${SOURCE}")
endif()

file(READ "${GENERATED}" gen_content)
file(READ "${SOURCE}"    src_content)

# ── Assertion 2: each injected inverse appears at least twice in gen ─────────
# (once from the forward source, once from the injection). The forward
# source has `a += 3;`, `a -= 3;`, `a *= 3;`, `a /= 3;` once each; the
# injections add one more of each (the LIFO-dual pattern swaps +/- and
# */, but the set of four statements is identical), so every one of the
# four patterns reaches count == 2 in the generated file. We check
# count >= 2 to stay tolerant of incidental whitespace.
set(_pb_patterns
    "a[ \t]*\\+=[ \t]*3"
    "a[ \t]*-=[ \t]*3"
    "a[ \t]*\\*=[ \t]*3"
    "a[ \t]*/=[ \t]*3")
foreach(rx IN LISTS _pb_patterns)
    string(REGEX MATCHALL "${rx}" hits "${gen_content}")
    list(LENGTH hits hit_count)
    if(hit_count LESS 2)
        message(FATAL_ERROR
            "check_example_constant_arith: expected at least 2 occurrences "
            "of `${rx}` in generated file (1 forward + 1 injected), "
            "found ${hit_count}.\n"
            "  file: ${GENERATED}")
    endif()
endforeach()

# ── Assertion 3: LIFO order of the injected block ───────────────────────────
# The forward chain is in source order `+=, -=, *=, /=`. LIFO uncompute
# therefore emits `*=, /=, +=, -=` at the bottom of the scope. We find
# the byte offset of the LAST forward statement (`a /= 3;`) and confirm
# that, after that offset, the four inverses appear in the expected
# order: `*=` first, `/=` second, `+=` third, `-=` fourth.
string(FIND "${gen_content}" "a /= 3;" last_fwd_offset)
if(last_fwd_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_constant_arith: could not find forward `a /= 3;` "
        "in generated file.")
endif()
string(LENGTH "${gen_content}" gen_len)
math(EXPR tail_start "${last_fwd_offset} + 7")
string(SUBSTRING "${gen_content}" ${tail_start} -1 tail)

foreach(seq IN ITEMS "a *= 3" "a /= 3" "a += 3" "a -= 3")
    string(FIND "${tail}" "${seq}" seq_offset)
    if(seq_offset EQUAL -1)
        message(FATAL_ERROR
            "check_example_constant_arith: expected injected `${seq};` "
            "after the last forward statement, but it was not found.\n"
            "  tail: <<<${tail}>>>")
    endif()
    math(EXPR tail_consumed "${seq_offset} + 6")
    string(SUBSTRING "${tail}" ${tail_consumed} -1 tail)
endforeach()

# ── Assertion 4: SOURCE remains clean ───────────────────────────────────────
# The forward source has `a *= 3;` exactly once (at the *= 3 position).
# If the transpiler ever contaminated its input, a second match would
# appear. We mirror the generated-file regex to stay tolerant of
# whitespace.
string(REGEX MATCHALL "a[ \t]*\\*=[ \t]*3" src_mul_hits "${src_content}")
list(LENGTH src_mul_hits src_mul_count)
if(NOT src_mul_count EQUAL 1)
    message(FATAL_ERROR
        "check_example_constant_arith: SOURCE ${SOURCE} contains "
        "`a *= 3` ${src_mul_count} times (expected 1). The transpiler "
        "must never rewrite its input in place.")
endif()

message(STATUS
    "check_example_constant_arith: OK — generated file has all four "
    "injected inverses in LIFO order and source is clean")
