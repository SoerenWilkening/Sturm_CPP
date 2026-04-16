// user_routine_runtime.cpp — Phase I / PI-7 transpiler INPUT fixture.
//
// Fifth gate-equivalence pair.  Whereas PG-7 / PH-6a / PH-6b / PI-7's
// predecessors exercise the OR matcher's per-branch, per-iteration, or
// nested-WHEN scope lowering, this fixture pins down the Phase I
// user-defined routine + automatic adjoint dispatch path:
//
//   * `ur_rotate_fwd_runtime(qbool& out, const qbool& in, int k)` —
//     forward user routine: flips `out` `k` times, emitting `k` X
//     gates on `out`'s qubit under an empty control stack.
//   * `ur_rotate_adj_runtime(qbool& out, const qbool& in, int k)` —
//     hand-written adjoint for the self-inverse X-chain: another
//     `k` flips, the byte-identical gate stream.
//   * `STURM_REGISTER_ADJOINT(ur_rotate_fwd_runtime,
//                             ur_rotate_adj_runtime)` — registers
//     the (forward, adjoint) pair via the PI-0 trait table.  The
//     PI-1 routine-registry matcher keys on this specialization of
//     `sturm::_detail::adjoint_of<decltype(&::ur_rotate_fwd_runtime)>`
//     and records the forward FunctionDecl* in the registry.
//
// Demo shape
// ----------
// void demo(const qbool& in) {
//     qbool tmp;
//     tmp.ensure_qubit();
//     ur_rotate_fwd_runtime(tmp, in, 3);
//     // transpiler injects: invert(ur_rotate_fwd_runtime)(tmp, in, 3);
// }
//
// PI-3 classification
// -------------------
// `tmp` is a VarDecl in the same CompoundStmt as the call, so PI-3
// classifies the output slot as `Intermediate` and PI-4's uncompute
// pass plants `invert(ur_rotate_fwd_runtime)(tmp, in, 3);` immediately
// before the demo's closing `}`.  The hand-written companion
// `user_routine_reference.cpp` spells the same forward + adjoint pair
// verbatim in LIFO order; the M12 harness byte-compares the captured
// gate streams.
//
// Gate-stream witness
// -------------------
// Forward: three `tmp.flip()` calls under an empty control stack ⇒
// three X gates on `tmp`'s qubit index.
// Adjoint (invert(...) → ur_rotate_adj_runtime): another three flips
// ⇒ three more X gates on the same index.
// `tmp`'s RAII destructor at the demo's close brace is release-only
// (no uncompute_op was set by flip()), so no trailing gates from the
// ancilla's release.  Total captured stream: six X gates.
//
// ODR note
// --------
// The harness links this TU together with user_routine_reference.cpp.
// Both fixture TUs define `demo(const qbool&)` inside their own
// namespace to avoid an ODR collision.  The forward / adjoint free
// functions live at global scope (STURM_REGISTER_ADJOINT uses `::fn`
// in its expansion — we would have to fully-qualify to put the
// routines inside a namespace), so the names include a per-TU suffix
// (`_runtime` vs `_reference`) to keep the two TUs linkable.
//
// Why the `__has_include` guard
// -----------------------------
// `sturm-transpile` runs Clang with a FixedCompilationDatabase that
// carries no include paths (see transpiler/src/main.cpp).  A bare
// `#include <sturm/sturm.hpp>` would therefore fail to resolve during
// the transpile step and the `sturm::_detail::adjoint_of` template
// plus the `qbool` type would never appear in the AST, silently
// disabling both the PI-1 and the PI-2 matchers.  To make this TU
// parseable by the transpiler AND compilable at runtime, the include
// is gated on `__has_include`: during transpile the preprocessor takes
// the stub branch (which provides minimal `sturm::qbool`, a
// `sturm::_detail::adjoint_of` primary template, a `STURM_REGISTER_ADJOINT`
// macro, and a trivial `sturm::invert(fn)` free template so the
// transpiler's emitted `invert(fn)(...)` call parses downstream when
// fed back through the transpiler idempotency run), and during the
// downstream compile the real headers are available so the gate
// emissions actually happen.
//
// The stub shape mirrors the skeleton the PI-1 / PI-2 matchers need:
// same qualified name for `adjoint_of`, same macro expansion shape.
// The transpiler cares about the structural pattern of the
// specialization + the call — not the semantic behaviour.
#if defined(__has_include) && __has_include(<sturm/sturm.hpp>)
#  include <sturm/sturm.hpp>
// The umbrella `sturm/sturm.hpp` already pulls in invert.hpp + the
// qbool type, but `flip()` lives in qbool_ops.hpp — include it
// explicitly so the routine bodies link against the real
// emit_X_lifted path.
#  include "sturm/qtypes/qbool.hpp"
#  include "sturm/qtypes/qbool_ops.hpp"
#  include "sturm/routines/invert.hpp"
#else
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    void ensure_qubit() {}
    qbool& flip() { return *this; }
};

namespace _detail {
template <typename FnPtr>
struct adjoint_of;
} // namespace _detail

template <typename R, typename... Args>
constexpr auto invert(R (*fn)(Args...)) noexcept {
    (void)fn;
    return _detail::adjoint_of<R (*)(Args...)>::value;
}
} // namespace sturm

#define STURM_REGISTER_ADJOINT(fn, adj)                                        \
    namespace sturm {                                                          \
    namespace _detail {                                                        \
    template <>                                                                \
    struct adjoint_of<decltype(&::fn)> {                                       \
        static constexpr auto value = &::adj;                                  \
    };                                                                         \
    }                                                                          \
    }
#endif

// ── Forward user routine (global scope) ─────────────────────────────────────
// Applies a classical X-chain of length `k` to `out`.  With an empty
// active control stack (the harness does not enter any WHEN() before
// calling demo()), each `flip()` emits a bare X gate on `out`'s qubit
// index — three gates for k=3.  `in` is unused at run time; it exists
// so the PI-2 matcher sees a const-qbool& input slot (outputs_mask bit
// clear) beside the non-const qbool& output slot (outputs_mask bit
// set) — the minimal signature that exercises both classifications.
//
// Must be at namespace scope (global here) because
// `STURM_REGISTER_ADJOINT` expands to a template specialization of
// `sturm::_detail::adjoint_of<decltype(&::ur_rotate_fwd_runtime)>` —
// the `::ur_rotate_fwd_runtime` nested-name-specifier requires a
// namespace-scope declaration.
inline void ur_rotate_fwd_runtime(sturm::qbool& out,
                                  const sturm::qbool& in,
                                  int k) {
    (void)in;
    for (int i = 0; i < k; ++i) {
        out.flip();
    }
}

// ── Hand-written adjoint (global scope) ─────────────────────────────────────
// For a self-inverse X-chain the adjoint is literally the forward
// routine run again — three flips undo three flips.  Same three X
// gates on the same qubit index as the forward.
inline void ur_rotate_adj_runtime(sturm::qbool& out,
                                  const sturm::qbool& in,
                                  int k) {
    (void)in;
    for (int i = 0; i < k; ++i) {
        out.flip();
    }
}

// ── Register the (forward, adjoint) pair ────────────────────────────────────
// Populates `sturm::_detail::adjoint_of<decltype(&::ur_rotate_fwd_runtime)>`
// with `value = &::ur_rotate_adj_runtime`.  The PI-1 matcher keys on
// this specialization and seeds the RoutineRegistry with
// `ur_rotate_fwd_runtime`'s canonical FunctionDecl*; the PI-2 matcher
// then emits a `QOpKind::USER_ROUTINE` QOperation when it sees the
// `ur_rotate_fwd_runtime(tmp, in, 3);` call in demo() below, and
// PI-4 renders that op as `invert(ur_rotate_fwd_runtime)(tmp, in, 3);`.
STURM_REGISTER_ADJOINT(ur_rotate_fwd_runtime, ur_rotate_adj_runtime)

namespace m12_user_routine_transpiled {
using sturm::qbool;

// Case 1 from the PI-2/PI-3 plan table: local-intermediate output.
// `tmp` is a qbool declared in the demo's top-level CompoundStmt, so
// PI-3's `classify_output` returns `Intermediate` and the M8 pass
// plants the injected `invert(...)` at the demo's closing brace.
// `in` is `const qbool&` so PI-2's `is_output_param` returns false
// for that slot — the only OUTPUT slot is `tmp`.  The call site uses
// unqualified `ur_rotate_fwd_runtime` and ADL finds the global
// routine; the PI-4 emitted `invert(ur_rotate_fwd_runtime)(...)` is
// resolved the same way at the injection site.
void demo(const qbool& in) {
    qbool tmp;
    tmp.ensure_qubit();
    ur_rotate_fwd_runtime(tmp, in, 3);
}

} // namespace m12_user_routine_transpiled
