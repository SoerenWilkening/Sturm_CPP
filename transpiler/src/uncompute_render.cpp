// uncompute_render.cpp — sturm-v0ur (LO-2 wiring): per-op inverse-text
// renderer extracted from `uncompute_pass.cpp`.
//
// Why a separate TU?
// ------------------
// Plan §9 (risk register, lossy_compound_reversibility) caps
// `uncompute_pass.cpp` at 600 LoC. Pre-LO-2 the file sat at 601 LoC; the
// LO-2 wiring (sturm-v0ur) needs to add a `register_external_cleanup()`
// hook (~20 LoC) on top of that. Hoisting the `render_uncompute()`
// switch (~320 LoC of per-`QOpKind` rendering rules — entirely
// orthogonal to the LIFO scheduler in `synthesize()`) into its own TU
// is the minimum-shrink the plan asks for: byte-identical output is
// preserved (this is the same `render_uncompute()` body, with only the
// surrounding file context changed); `uncompute_pass.cpp` shrinks well
// below the 600-LoC line; and the logical boundary between "what to
// emit per op" (this file) and "where to emit it inside a scope"
// (uncompute_pass.cpp) becomes explicit.
//
// Header contract is unchanged. `render_uncompute()` is still declared
// in `sturm/transpile/uncompute_pass.hpp`; existing callers (the
// adjoint emitter, the M8 synthesis pass) pull in the same header and
// see no API delta. Only the linker sees the new TU.

#include "sturm/transpile/uncompute_pass.hpp"

#include "sturm/transpile/qir.hpp"
#include "sturm/transpile/plugin_api.hpp"

#include <sstream>
#include <string>

namespace sturm::transpile {

// Build the source-text snippet for a single forward op's inverse. The
// exact format is locked down by the M8 tests and the PRD's output
// contract: four-space indent, `uncompute_or(<result>, <op0>, <op1>);\n`
// (and analogous per-kind forms for later phases).
//
// Phase A adds NOT. Per the roadmap, NOT is its own inverse: applying the
// same `~` to the result qubit uncomputes it. The emitted form is
// `<result> = ~<result>;` rather than a separate `uncompute_not(...)`
// free function, because there is no meaningful out-of-place inverse for
// a single-qubit X gate — the in-place form composes to identity with
// zero ancilla cost.
//
// PM4-3: the `registry` parameter is consulted ONLY for
// `case QOpKind::PLUGIN:` — every in-tree kind ignores it. Passed by
// const pointer (not reference) so a hand-built test fixture without a
// Registry can pass `nullptr`; in that case, a `QOpKind::PLUGIN` op
// renders to an empty string (same defensive posture as every other
// render case on malformed input).
//
// Phase R R-1 (sturm-88d7.2): promoted from file-local to namespace-
// scope so the `adjoint_emitter` module can reuse it at the statement
// level (walking a routine body in reverse and emitting one
// rendered-inverse line per statement). See `uncompute_pass.hpp` for
// the exposed contract.
std::string render_uncompute(const QOperation& op,
                             const plugin::Registry* registry) {
    std::ostringstream os;
    switch (op.kind) {
    case QOpKind::OR: {
        // Defensive: the MVP matcher always produces exactly two operands
        // for OR. If a future IR consumer seeds a malformed op, emit
        // nothing so we do not inject invalid C++ into the user's file.
        if (op.operands.size() != 2) return {};
        os << "    uncompute_or(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::AND: {
        // Phase E: exact analogue of the OR case. The Phase E compound
        // matcher (PE-4) records `qbool r = a & b;` with two operands
        // (the two qbool inputs); the inverse is a free-function call
        // `uncompute_and(r, a, b);` declared in
        // include/sturm/uncompute/uncompute_api.hpp and landed in PE-0
        // (sturm-oheo). Malformed seeds (operand count != 2) render
        // nothing, matching the OR guard.
        if (op.operands.size() != 2) return {};
        os << "    uncompute_and(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::NOT: {
        // NOT is self-inverse: re-applying `~` to the result qubit
        // uncomputes it. The operand list is unused in the emission
        // (the inverse touches only the result) but must be non-empty —
        // an op with no operand would indicate a malformed IR seed.
        if (op.operands.size() != 1) return {};
        os << "    " << op.result.name << " = ~" << op.result.name << ";\n";
        break;
    }
    case QOpKind::XOR: {
        // XOR is self-inverse with a two-step reduction: applying the
        // same two operands via `^=` to the result undoes it, because
        // (a^b)^a^b == 0. Emitted on two separate source lines for
        // readability; the order is lhs then rhs, matching the forward
        // source order of the original `a ^ b`.
        if (op.operands.size() != 2) return {};
        os << "    " << op.result.name << " ^= " << op.operands[0].name
           << ";\n"
           << "    " << op.result.name << " ^= " << op.operands[1].name
           << ";\n";
        break;
    }
    case QOpKind::XOR_ASSIGN: {
        // `a ^= b;` is self-adjoint: applying the same statement twice
        // returns a to its original state. The emitted inverse is a
        // verbatim re-emission of the forward call.
        if (op.operands.size() != 1) return {};
        os << "    " << op.result.name << " ^= " << op.operands[0].name
           << ";\n";
        break;
    }
    case QOpKind::ADD_ASSIGN_CONST: {
        // Phase B: `a += C;` where C is a compile-time-readable classical
        // constant source fragment stored verbatim in operands[0].name.
        // The inverse is the dual operator over the same constant.
        if (op.operands.size() != 1) return {};
        os << "    " << op.result.name << " -= " << op.operands[0].name
           << ";\n";
        break;
    }
    case QOpKind::SUB_ASSIGN_CONST: {
        if (op.operands.size() != 1) return {};
        os << "    " << op.result.name << " += " << op.operands[0].name
           << ";\n";
        break;
    }
    case QOpKind::MUL_ASSIGN_CONST: {
        if (op.operands.size() != 1) return {};
        os << "    " << op.result.name << " /= " << op.operands[0].name
           << ";\n";
        break;
    }
    case QOpKind::DIV_ASSIGN_CONST: {
        if (op.operands.size() != 1) return {};
        os << "    " << op.result.name << " *= " << op.operands[0].name
           << ";\n";
        break;
    }
    case QOpKind::THETA_ADD_ASSIGN_CONST: {
        // Phase N PN-4: `q.theta() += C;` where C is a verbatim
        // double-valued source fragment captured in operands[0].name (via
        // Lexer::getSourceText on the PN-2 matcher's hasArgument(1, ...)
        // capture). The inverse is the sign-flipped compound-assign on the
        // same ThetaProxy, which the runtime handles via the self-dual
        // operator-= at include/sturm/qtypes/qint_core.hpp:305
        // (ThetaProxy::operator-=(double d) { operator+=(-d); }). Inline
        // two-character sign flip — no free-function helper in
        // uncompute_api.hpp — mirroring the Phase B precedent above.
        if (op.operands.size() != 1) return {};
        os << "    " << op.result.name << ".theta() -= "
           << op.operands[0].name << ";\n";
        break;
    }
    case QOpKind::THETA_SUB_ASSIGN_CONST: {
        // Phase N PN-4: dual of THETA_ADD_ASSIGN_CONST — forward `q.theta()
        // -= C;` inverts to `q.theta() += C;`.
        if (op.operands.size() != 1) return {};
        os << "    " << op.result.name << ".theta() += "
           << op.operands[0].name << ";\n";
        break;
    }
    case QOpKind::PHI_ADD_ASSIGN_CONST: {
        // Phase N PN-4: phi (phase) rotation analogue of
        // THETA_ADD_ASSIGN_CONST. The runtime's self-dual PhiProxy
        // operator-= lives at include/sturm/qtypes/qint_core.hpp:372 and
        // dispatches -delta through the existing Rz(θ) emission path.
        if (op.operands.size() != 1) return {};
        os << "    " << op.result.name << ".phi() -= "
           << op.operands[0].name << ";\n";
        break;
    }
    case QOpKind::PHI_SUB_ASSIGN_CONST: {
        // Phase N PN-4: dual of PHI_ADD_ASSIGN_CONST — forward `q.phi() -=
        // C;` inverts to `q.phi() += C;`.
        if (op.operands.size() != 1) return {};
        os << "    " << op.result.name << ".phi() += "
           << op.operands[0].name << ";\n";
        break;
    }
    case QOpKind::ADD_ASSIGN_QINT: {
        // Phase C: `a += b;` where b is another qint named in source.
        // operands[0].name carries the verbatim RHS identifier. The
        // inverse is the `uncompute_add_qint` free function declared in
        // include/sturm/uncompute/uncompute_api.hpp, which delegates to
        // the forward `-=` compound-assign on the runtime side.
        if (op.operands.size() != 1) return {};
        os << "    uncompute_add_qint(" << op.result.name << ", "
           << op.operands[0].name << ");\n";
        break;
    }
    case QOpKind::SUB_ASSIGN_QINT: {
        if (op.operands.size() != 1) return {};
        os << "    uncompute_sub_qint(" << op.result.name << ", "
           << op.operands[0].name << ");\n";
        break;
    }
    case QOpKind::MUL_ASSIGN_QINT: {
        if (op.operands.size() != 1) return {};
        os << "    uncompute_mul_qint(" << op.result.name << ", "
           << op.operands[0].name << ");\n";
        break;
    }
    case QOpKind::DIV_ASSIGN_QINT: {
        if (op.operands.size() != 1) return {};
        os << "    uncompute_div_qint(" << op.result.name << ", "
           << op.operands[0].name << ");\n";
        break;
    }
    case QOpKind::MOD_ASSIGN_QINT: {
        if (op.operands.size() != 1) return {};
        os << "    uncompute_mod_qint(" << op.result.name << ", "
           << op.operands[0].name << ");\n";
        break;
    }
    case QOpKind::EQ_QINT: {
        // Phase D: `qbool c = a == b;` — c is the produced qbool result,
        // operands[0] is the LHS qint identifier, operands[1] is the RHS
        // qint identifier. The inverse is the `uncompute_eq_qint` free
        // function declared in include/sturm/uncompute/uncompute_api.hpp,
        // which re-dispatches to the self-adjoint DSL `lib_eq_dsl` in
        // include/sturm/lib/compare_dsl.hpp. Two operands, not one, because
        // the comparator adjoint needs both inputs to flip the result bit
        // back to |0⟩.
        if (op.operands.size() != 2) return {};
        os << "    uncompute_eq_qint(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::NE_QINT: {
        if (op.operands.size() != 2) return {};
        os << "    uncompute_ne_qint(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::LT_QINT: {
        if (op.operands.size() != 2) return {};
        os << "    uncompute_lt_qint(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::LE_QINT: {
        if (op.operands.size() != 2) return {};
        os << "    uncompute_le_qint(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::GT_QINT: {
        if (op.operands.size() != 2) return {};
        os << "    uncompute_gt_qint(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::GE_QINT: {
        if (op.operands.size() != 2) return {};
        os << "    uncompute_ge_qint(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::USER_ROUTINE: {
        // Phase I PI-4: render the user-routine's adjoint dispatch.
        // The emitted form is the post-sturm-bdmh NTTP shape
        //   "    invert<&<routine_name>>()(<op0>, <op1>, ...);\n"
        // with operands listed in source order. There is NO result-name
        // prefix — a USER_ROUTINE op mutates through its output
        // parameters (flagged by `outputs_mask`) and has no single
        // named result. The four-space leading indent matches every
        // other kind so the emitter injects uniform text into the
        // user's source.
        //
        // Why NTTP at the call site (and not the legacy `invert(<name>)
        // (...)` form): see include/sturm/routines/invert.hpp. After
        // sturm-bdmh the runtime trait `sturm::_detail::adjoint_of<auto
        // Fn>` is keyed on the function-pointer VALUE, not the type, so
        // two function-template instantiations sharing a signature can
        // each register a distinct adjoint without colliding. The only
        // C++20-portable way to feed the function pointer into the
        // trait is as a non-type template argument at the call site.
        //
        // Defensive: if `routine_name` is empty (should never happen
        // in practice — the PI-2 matcher always populates it, and a
        // FunctionDecl with no name could not have been registered in
        // the PI-1 routine registry — but a hand-built fixture or a
        // future IR consumer could seed an empty one), emit nothing
        // rather than render `invert<&>()(...);` which would not
        // compile.
        if (op.routine_name.empty()) return {};
        // Qualified `sturm::invert<...>()` — ADL does not propagate
        // through a template-id (no first-positional argument from
        // which to deduce associated namespaces, vs the legacy
        // `invert(&fn)` shape which let ADL find `sturm::invert`
        // through `&fn`'s sturm-namespaced argument types). The
        // qualification is the simplest portable answer; alternatives
        // (synthesising a `using sturm::invert;` at injection scope)
        // would require coordinating with the surrounding TU's
        // declaration set, which is out of scope here.
        os << "    sturm::invert<&" << op.routine_name << ">()(";
        for (std::size_t i = 0; i < op.operands.size(); ++i) {
            if (i != 0) os << ", ";
            os << op.operands[i].name;
        }
        os << ");\n";
        break;
    }
    case QOpKind::CCNOT_INPLACE: {
        // Phase J PJ-1c: zero-ancilla fusion seeded by the PJ-1d peephole
        // matcher from the adjacent pair
        //     qbool __t = a & b;
        //     x ^= __t;
        // when `__t` has exactly one reader. Operand shape mirrors OR /
        // AND: one result (the `x` target of the in-place flip) plus two
        // named operand QValueRefs (the two qbool controls).
        //
        // Self-adjoint: `ccnot_inplace(x, a, b)` is a single CCX(a, b, x)
        // that is its own inverse, so the forward emission (a verbatim
        // QReplacement text applied by the matcher over the fused pair)
        // and the uncompute emission (this render case) share a single
        // identifier — running the helper a second time at the uncompute
        // point undoes the forward flip. The four-space leading indent
        // matches every other kind so the emitter injects uniform text.
        //
        // Defensive: if the operand count is not exactly 2 — a malformed
        // hand-built fixture or a future IR consumer seeding a bad op —
        // emit nothing so we do not inject invalid C++ into the user's
        // file. Mirrors the identical guard on OR / AND.
        if (op.operands.size() != 2) return {};
        os << "    ccnot_inplace(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::PLUGIN: {
        // Phase M PM4-3: plugin-registered op. Consult the per-consumer
        // Registry via `find_render_fn(plugin_kind_id)` and invoke the
        // returned `UncomputeRenderFn`. The returned string is taken
        // verbatim — the plugin is responsible for the four-space
        // indent + trailing '\n' invariant every in-tree renderer
        // follows (documented in `plugin_api.hpp`'s
        // `UncomputeRenderFn` doc).
        //
        // Defensive guards:
        //
        //   1. `registry == nullptr` — a hand-built test fixture or a
        //      pre-PM4 caller that forgot to thread the Registry.
        //      Emitting nothing matches the posture other kinds take on
        //      malformed input, and the degenerate case is unreachable
        //      from the production consumer (which always passes a
        //      valid Registry).
        //
        //   2. `plugin_kind_id` empty — a hand-built op missing the
        //      key. Skipping keeps the malformed-input posture uniform
        //      with USER_ROUTINE's empty-routine-name guard.
        //
        //   3. `find_render_fn(kind_id)` returns null — the plugin's
        //      registration was dropped (collision rejected) or never
        //      ran. Emitting nothing lets the scope still close cleanly
        //      and the build keep going; a missing renderer for a
        //      claimed kind_id is a build-by-build mismatch the user
        //      should see via the host's own dlopen-side error, not a
        //      nested segfault inside a rewrite pass.
        //
        //   4. The renderer's `std::function<>` target is empty (moved
        //      from or default-constructed) — the UncomputeRenderFn
        //      registered was uninitialized. Same posture: emit
        //      nothing.
        if (registry == nullptr) return {};
        if (op.plugin_kind_id.empty()) return {};
        const plugin::UncomputeRenderFn* fn =
            registry->find_render_fn(op.plugin_kind_id);
        if (fn == nullptr) return {};
        if (!*fn) return {};
        return (*fn)(op);
    }
    // No `default:` — adding a new QOpKind should fail the build here
    // until every downstream consumer is updated. (Compilers warn on
    // missing enum cases when default is absent.)
    }
    return os.str();
}

} // namespace sturm::transpile
