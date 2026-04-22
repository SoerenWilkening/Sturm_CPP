cmake_minimum_required(VERSION 3.16)
enable_language(C)

# ── Example: qint OR + ASCII circuit drawing ────────────────────────────────
# Routed through sturm-transpile via the add_quantum_executable() helper
# (see cmake/SturmTranspile.cmake).
add_quantum_executable(example_or_circuit
    or_circuit.cpp
)

# Runtime implementation sources are not candidates for the transpiler;
# attach them after target creation so they bypass the transpile step.
target_sources(example_or_circuit PRIVATE
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_link_libraries(example_or_circuit PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)

# ── Example: qint constant-arithmetic compound-assigns (Phase B) ────────────
# Routed through sturm-transpile so each `a += 3; a -= 3; a *= 3; a /= 3;`
# statement gets a dual-operator inverse injected before the enclosing
# scope closes. Same helper / bypass pattern as example_or_circuit.
add_quantum_executable(example_constant_arith
    constant_arith.cpp
)
target_sources(example_constant_arith PRIVATE
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_link_libraries(example_constant_arith PRIVATE orkan_headers)

# ── Example: qint qint-qint compound-assigns (Phase C) ──────────────────────
# Routed through sturm-transpile so each `a += b; a -= b; a *= b; a /= b;
# a %= b;` statement gets a `uncompute_*_qint(a, b);` free-function call
# injected before the enclosing scope closes. Same helper / bypass pattern
# as example_constant_arith.
add_quantum_executable(example_qint_arith
    qint_arith.cpp
)
target_sources(example_qint_arith PRIVATE
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_link_libraries(example_qint_arith PRIVATE orkan_headers)

# ── Example: qint qint-qint comparison operators (Phase D) ─────────────────
# Routed through sturm-transpile so each `qbool c_xx = a OP b;` VarDecl
# initializer (for OP in {==, !=, <, <=, >, >=}) gets a
# `uncompute_{eq,ne,lt,le,gt,ge}_qint(c_xx, a, b);` free-function call
# injected before the enclosing scope closes. Same helper / bypass pattern
# as example_qint_arith.
add_quantum_executable(example_comparison
    comparison.cpp
)
target_sources(example_comparison PRIVATE
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_link_libraries(example_comparison PRIVATE orkan_headers)

# ── Example: compound qbool expressions (Phase E) ──────────────────────────
# Routed through sturm-transpile so the compound `qbool r = (b | c) & d;`
# initializer gets REPLACED with a flat two-decl sequence (one fresh
# `__stu_t<N>` for the inner OR, the original VarDecl name on the outer
# AND) and a LIFO `uncompute_and` / `uncompute_or` pair injected before
# the enclosing scope closes. Same helper / bypass pattern as
# example_comparison; the new uncompute_and runtime symbol added in
# Phase E PE-0 is linked from src/sturm/uncompute/uncompute_api.cpp.
add_quantum_executable(example_compound_expression
    compound_expression.cpp
)
target_sources(example_compound_expression PRIVATE
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_link_libraries(example_compound_expression PRIVATE orkan_headers)

# ── Example: compound-`WHEN` integration (Phase F) ──────────────────────────
# Routed through sturm-transpile so the `WHEN((b | c) & d) { ... }` macro
# invocation is rewritten: two flat `qbool __stu_tN = ...;` decls are
# injected ABOVE the WHEN line, the WHEN argument is substituted to the
# outermost `__stu_t1` temp, and a LIFO `uncompute_and(__stu_t1,
# __stu_t0, d); / uncompute_or(__stu_t0, b, c);` pair is planted
# directly after the WHEN body's closing brace (still inside the
# enclosing inner scope). Same helper / bypass pattern as
# example_compound_expression; the Phase F matcher lives in
# transpiler/src/matcher_when_lift.cpp.
add_quantum_executable(example_when_integration
    when_integration.cpp
)
target_sources(example_when_integration PRIVATE
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_link_libraries(example_when_integration PRIVATE orkan_headers)

# ── Example: named-named nested `WHEN` AND-fold (Phase G) ───────────────────
# Routed through sturm-transpile so each `WHEN(outer) { WHEN(inner) { ... } }`
# named-named pair is rewritten: a `qbool __stu_ctrl<N> = outer & inner;`
# decl is injected immediately ABOVE the inner WHEN line, the inner
# WHEN argument is substituted to `__stu_ctrl<N>`, and a matching
# `uncompute_and(__stu_ctrl<N>, outer, inner);` call is planted
# directly after the inner WHEN body's closing brace (still inside
# the outer scope).  Depth-3 cascades fire twice — once per adjacent
# pair — with a persistent `FreshNameAllocator::next_ctrl()` counter
# so the two `__stu_ctrl` temps never collide.  Same helper / bypass
# pattern as example_when_integration; the Phase G matcher lives in
# transpiler/src/matcher_when_nested.cpp, and the runtime
# `uncompute_and` symbol resolves via
# src/sturm/uncompute/uncompute_api.cpp.
add_quantum_executable(example_nested_when
    nested_when.cpp
)
target_sources(example_nested_when PRIVATE
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_link_libraries(example_nested_when PRIVATE orkan_headers)

# ── Example: classical control flow around quantum ops (Phase H) ───────────
# Routed through sturm-transpile so each `qbool <var> = a | b;` inside a
# for-loop body or if/else branch gets a `uncompute_or(<var>, a, b);` call
# planted before that body's closing `}` — once per iteration for the
# for-loop, and one distinct per-branch intermediate for the if/else.
# Phase H fills three gaps: PH-1 (scope-finder refactor keying QScopes on
# either CompoundStmt or BracelessBody anchors), PH-2
# (matcher_brace_wrap.cpp auto-synthesising `{ }` around braceless bodies
# that contain a quantum op), and PH-3 (matcher_outer_var_guard.cpp
# diagnostic rejecting outer-scoped qbool/qint mutations inside a
# loop/branch/WHEN body).  This example exercises the happy path for both
# control-flow kinds; the PH-3 diagnostic path is covered by the hermetic
# `for_outer_xor_reject` fixture.  Same helper / bypass pattern as
# example_nested_when; the Phase H matchers live in
# transpiler/src/matcher_brace_wrap.cpp and
# transpiler/src/matcher_outer_var_guard.cpp, and the runtime
# `uncompute_or` symbol resolves via src/sturm/uncompute/uncompute_api.cpp.
add_quantum_executable(example_control_flow
    control_flow.cpp
)
target_sources(example_control_flow PRIVATE
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_link_libraries(example_control_flow PRIVATE orkan_headers)

# ── Example: user-defined routine + STURM_REGISTER_ADJOINT demo (Phase I) ───
# Routed through sturm-transpile so every `callExpr` to a forward routine
# paired with a hand-written adjoint via `STURM_REGISTER_ADJOINT(fwd, adj)`
# gets an `invert(<name>)(<args...>);` injected before the enclosing
# scope's close brace — UNLESS PI-3's output-ownership classifier tells
# PI-4 to skip.  This example exercises the two canonical cases: a
# call whose output backs a local-intermediate qbool (Intermediate
# class → injection fires) and a call inside a helper function whose
# output backs a ParmVarDecl (Final class → no injection).  Same
# helper / bypass pattern as example_control_flow; the Phase I
# matchers live in transpiler/src/matcher_user_routine.cpp and
# transpiler/src/routine_registry.cpp, and the runtime `invert(...)`
# template resolves via include/sturm/routines/invert.hpp (re-exported
# by include/sturm/sturm.hpp).
add_quantum_executable(example_user_routine
    user_routine.cpp
)
target_sources(example_user_routine PRIVATE
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_link_libraries(example_user_routine PRIVATE orkan_headers)

# ── Example: combined Phase A + B + C demo ──────────────────────────────────
# Routed through sturm-transpile so every forward pattern from all three
# phases receives its inverse injection in one file. LIFO rewind runs
# Phase C → Phase B → Phase A just before the inner scope closes. Same
# helper / bypass pattern as the single-phase examples above.
add_quantum_executable(example_phase_abc_demo
    phase_abc_demo.cpp
)
target_sources(example_phase_abc_demo PRIVATE
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_link_libraries(example_phase_abc_demo PRIVATE orkan_headers)

# ── Example: zero-ancilla fusion peephole demo (Phase J PJ-1) ───────────────
# Routed through sturm-transpile so the adjacent pair
#   qbool __t = a & b;
#   x ^= __t;
# gets COLLAPSED in-place into a single forward `ccnot_inplace(x, a, b);`
# call (PJ-1d `QReplacement` spans both stmts), and a matching self-
# adjoint `ccnot_inplace(x, a, b);` is planted before the enclosing
# scope's closing brace by the PJ-1c render case in
# transpiler/src/uncompute_pass.cpp — CCX is its own inverse, so the
# forward emission and the uncompute emission share one symbol.  The
# PJ-1 fusion erases the ancilla qubit the unfused path would have
# allocated for `__t`; the rewrite composes with every Phase A–I
# matcher upstream (register_ccnot_fuse_matcher wires BEFORE the
# Phase E compound-flatten matcher and the Phase A `^=` matcher per
# the PJ-1f ordering rationale).  Same helper / bypass pattern as
# example_user_routine; the new `ccnot_inplace` runtime symbol
# resolves via src/sturm/uncompute/uncompute_api.cpp.
add_quantum_executable(example_zero_ancilla_fusion
    zero_ancilla_fusion.cpp
)
target_sources(example_zero_ancilla_fusion PRIVATE
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_link_libraries(example_zero_ancilla_fusion PRIVATE orkan_headers)

# ── Example: dead-ancilla elimination demo (Phase J PJ-4) ───────────────────
# Routed through sturm-transpile so every `qbool t = <qbool-operand bitwise
# expr>;` VarDecl with ZERO readers in its enclosing scope gets REPLACED
# in-place by empty text — and its range is pushed into
# `QUnit::eliminated_stmt_ranges` so the downstream Phase A/E/MVP OR
# matchers early-return via `apply_eliminated_stmt_guards`.  Net effect:
# the dead decl vanishes from the generated sibling, no forward gates
# emit, and no `uncompute_or(...)` lands at scope close.  The reader-
# reject path passes through the Phase A PA-3 `^=` matcher and the MVP
# OR matcher unchanged — the `live` decl keeps its forward pair, gains
# a PA-3 self-adjoint inverse, and gains an `uncompute_or(live, a, b);`
# at scope close.  Same helper / bypass pattern as
# example_zero_ancilla_fusion; the Phase J PJ-4a matcher lives in
# transpiler/src/matcher_dead_ancilla.cpp and its registration before
# the Phase A/E matchers is pinned in transpiler/src/main.cpp.
add_quantum_executable(example_dead_ancilla
    dead_ancilla.cpp
)
target_sources(example_dead_ancilla PRIVATE
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_link_libraries(example_dead_ancilla PRIVATE orkan_headers)

# ── Example: uncompute-hoisting demo (Phase J PJ-3) ─────────────────────────
# Routed through sturm-transpile so a `qbool t = a | b;` VarDecl inside a
# for-loop body whose operands are loop-invariant gets its uncompute
# RELOCATED: the PJ-3d matcher (`register_hoist_invariant_matcher` in
# transpiler/src/matcher_hoist_invariant.cpp) fires on the loop-body
# scope, verifies `classify_scope_kind == LoopBody`, confirms every
# operand is loop-invariant per `detail::expr_is_loop_invariant`, and
# sets `op.hoist_to_override` to the ENCLOSING scope's close brace.
# The M8 uncompute-synthesis pass then plants
# `uncompute_or(t, a, b);` AFTER the for-loop's closing `}` rather
# than inside the for-body — amortizing the inverse cost across the
# loop's iteration count.  PJ-3d only moves the UNCOMPUTE anchor;
# the forward compute text-move is a later-phase concern (PJ-3g+),
# so the forward `qbool t = a | b;` stays per-iteration.  The
# outer-scope `qbool t;` predecl in the example's main() body is a
# pragmatic workaround for the PJ-3d limitation: the hoisted
# uncompute references `t` at a location where the inner-loop
# `qbool t` decl is no longer in scope.  Same helper / bypass
# pattern as example_dead_ancilla; the PJ-3d matcher's registration
# LAST in transpiler/src/main.cpp (after every Phase A–I per-op
# matcher and after PJ-1f/PJ-4b) is pinned by the PJ-3e ordering.
add_quantum_executable(example_uncompute_hoisting
    uncompute_hoisting.cpp
)
target_sources(example_uncompute_hoisting PRIVATE
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_link_libraries(example_uncompute_hoisting PRIVATE orkan_headers)

# ── Example: peephole gate reordering demo (Phase M PM5) ────────────────────
# Routed through sturm-transpile so the canonical PM5 triple-shape source
#   qbool r = (a & b) | c;    // PE-4 lifts inner AND into __stu_t0 synth
#   y ^= z;                    // B — bit-disjoint XOR_ASSIGN
#   x ^= r;                    // C — XOR_ASSIGN consumer
# flows through the full pipeline (Phase E / PE-4 compound-flatten +
# Phase A / PA-3 `^=` matcher + MVP OR / Phase E LIFO uncompute emission
# at scope close).  PM5-5's peephole reorder matcher
# (`register_peephole_reorder_matcher` in
# transpiler/src/matcher_peephole_reorder.cpp) inspects the final op
# list but conservatively refuses on the current MVP matcher set
# because the PE-4 outer OR wedges between the inner AND and the user-
# written `^=` consumer in `scope.ops`, breaking Gate 1's adjacent-
# triple precondition.  This example captures the CURRENT pipeline
# emission for the canonical triple-shape source, pinning the pre-
# reorder layout so any future enablement (e.g. a standalone AND
# matcher whose decl-sink emits synthetic `__stu_t*` temps without a
# wedge OR) produces a visible delta in the generated sibling.  Same
# helper / bypass pattern as example_uncompute_hoisting; the PM5-5
# matcher's registration LAST in transpiler/src/transpile_consumer.cpp
# (after every Phase A–I per-op matcher and after PJ-1/PJ-3/PJ-4) is
# pinned by the PM5-6 ordering.
add_quantum_executable(example_peephole_reorder
    peephole_reorder.cpp
)
target_sources(example_peephole_reorder PRIVATE
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_link_libraries(example_peephole_reorder PRIVATE orkan_headers)

# ── Example: Phase N rotation compound-assigns (P5 items 2 & 3) ────────────
# Routed through sturm-transpile so each of the four rotation compound-
# assigns (`q.theta() += d;`, `q.theta() -= d;`, `q.phi() += d;`,
# `q.phi() -= d;`) receives a sign-flipped inline inverse before the
# enclosing scope closes (LIFO order), and the depth-1 WHEN-guarded
# rotation receives its inverse INSIDE the WHEN body so the control
# scope covers both directions.  Exercises the PN-2 rotation matchers
# (`register_theta_add_matcher`, `register_theta_sub_matcher`,
# `register_phi_add_matcher`, `register_phi_sub_matcher` in
# transpiler/src/matcher_rotation.cpp) and the PN-4 uncompute-pass arms
# in transpiler/src/uncompute_pass.cpp.  The example pins the B5
# invariant (depth ≤ 1 control chains per Phase G AND-fold) by
# construction — a depth-2 nested WHEN holding a rotation would violate
# B5, and the single-depth construction here is the load-bearing
# invariant.  Same helper / bypass pattern as
# example_peephole_reorder; the PN-2 matchers register through the
# PM4-6 `STURM_REGISTER_PLUGIN(PNRotationPlugin)` dogfood path
# (no consumer-side edit needed), and the runtime's self-dual
# ThetaProxy / PhiProxy `operator-=` at
# include/sturm/qtypes/qint_core.hpp:305,372 dispatches `-delta`
# through the existing Ry / Rz emission path so the forward + inverse
# GateRecord stream cancels to identity.
add_quantum_executable(example_rotations
    rotations.cpp
)
target_sources(example_rotations PRIVATE
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_link_libraries(example_rotations PRIVATE orkan_headers)

# ── Example: Phase M PM1-8 in-memory transpile tutorial ─────────────────────
# Three-phase composition demo that exercises Phase E (compound
# qbool expressions), Phase F (compound-WHEN integration), and Phase J
# PJ-1 (zero-ancilla fusion) in a single translation unit — each
# rewrite confined to its own block scope so the injections compose
# cleanly without matcher cross-talk.  Routed through the default
# add_quantum_executable() plugin mode added in PM1-5, so the
# rewrite lives in memory: clang++ loads
# $<TARGET_FILE:sturm-transpile-plugin>, runs the shared
# TranspileConsumer under a ReplaceAction against the AST, then
# drives a nested CompilerInvocation + EmitObjAction over the
# rewritten buffer.  The plugin's `dump-to=<path>` arg (wired in
# cmake/SturmTranspile.cmake) mirrors the rewritten buffer onto disk
# at ${CMAKE_BINARY_DIR}/sturm_gen/examples/in_memory_transpile.cpp
# so the PM1-8 injected-observability CTest continues to work
# unchanged.  Same helper / bypass pattern as every preceding
# Phase B–J example above.
add_quantum_executable(example_in_memory_transpile
    in_memory_transpile.cpp
)
target_sources(example_in_memory_transpile PRIVATE
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_link_libraries(example_in_memory_transpile PRIVATE orkan_headers)

# ── PM4-7: demo plugin exercising the PM4-1 / PM4-2 extension surface ───────
# Builds `sturm-pm4-demo-plugin` as a MODULE shared library. The plugin is
# dlopen-only — it is not wired into any `add_quantum_executable` target.
# Consumers: the PM4-7 unit test `test_plugin_demo` (links the TU
# directly), the PM4-8 smoke `pm4_smoke_dlopen` (dlopen's the .so), and
# the PM4-10 link-time smoke `pm4_smoke_linktime` (bakes the TU into the
# host binary under `STURM_PM4_LINK_DEMO=ON`). Separate subdirectory so
# the plugin's CMake stays self-contained and the `include/link` rules
# do not bleed into the other examples.
add_subdirectory(plugin_demo)
