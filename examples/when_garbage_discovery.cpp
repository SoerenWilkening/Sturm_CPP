#define STURM_BACKEND_ENABLED 1

#include "sturm/backend/exec_append.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/control/when.hpp"
#include "sturm/control/garbage_registry.hpp"
#include "sturm/control/when_scope_garbage.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/sturm.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

// ─────────────────────────────────────────────────────────────────────────────
// sturm-h5it + sturm-pqs0 + sturm-njul: WHEN-nested lossy compound-assigns
// ─────────────────────────────────────────────────────────────────────────────
//
// This example is a RUNTIME behavior demo, not a transpiler rewrite demo.
// The generated sibling at
//     build/sturm_gen/examples/when_garbage_discovery.cpp
// is essentially identical to this source — no Phase A–N matcher fires,
// because the sturm-h5it epic's fix is entirely in the qint operator
// implementations (include/sturm/qtypes/qint_{arith,bitwise}_v3.hpp),
// not in a source-to-source rewrite.
//
// What changed with sturm-h5it (the bug it fixed)
// -----------------------------------------------
// Before the epic, inside a `WHEN(ctrl) { a &= b; }` scope (and likewise
// for `|=`, `*=`, `/=`, `%=`), the operator's tail would:
//     (1) compute the result register via lifted CCX / CSWAP gates, and
//     (2) UNCONDITIONALLY release the old `a` register and pointer-relabel
//         `a.qubits[]` to the new result register.
// Step (2) was silently wrong whenever `ctrl` held any |0> amplitude: in
// that branch the compute gates did nothing, but the relabel happened
// regardless, which zeroed `a` in the ctrl=|0> branch — information loss.
//
// After the epic, the tail splits on the control stack:
//   - Uncontrolled path: same release + pointer-relabel as before (safe).
//   - Controlled path: per-bit Fredkin (CSWAP) between a.qubits[i] and the
//     fresh result register via the `a^=b; b^=a; a^=b` idiom that
//     BitProxy auto-lifts to CCX under WHEN. After the Fredkin the
//     caller's `a.qubits[]` STILL points at the same physical wires it
//     did on entry (in-place at the qubit level), the old-A value now
//     lives in the leaked result register, and that register is pushed
//     into `sturm::detail::garbage_registry` with its tag and width.
//
// sturm-pqs0 extended the registry to also track the extra output wires
// that multi-output reversible ops leave behind even on the uncontrolled
// path: the upper W of `*=`'s 2W Cuccaro product (MUL_UPPER_W) and the
// remainder register of `/=` (DIV_REMAINDER).
//
// sturm-njul adds the scope-exit consumer: `detail::WhenScopeGarbage`
// RAII-hooks a baseline on WHEN entry and pops all records added during
// the scope at WHEN exit. It does NOT emit any uncomputation gates —
// real gate-level uncomputation is a dedicated future design session.
// Today the consumer is a bookkeeping sink, optionally surfacing a one-
// line stderr diagnostic when `STURM_GARBAGE_REPORT` is set.
//
// HOW TO RUN
// ----------
//     cmake --build build --target example_when_garbage_discovery --parallel 6
//     ./build/examples/example_when_garbage_discovery
//
// What you should see
// -------------------
//   - Registry size before the WHEN is 0.
//   - Inside the WHEN, after `a &= b`, the registry grew by exactly one
//     record tagged `AND_ASSIGN`, carrying the ctrl qubit, W, and the
//     leaked result-register qubit indices.
//   - `a.qubits[]` inside the WHEN still points at exactly the same
//     physical wires `a` was allocated on before the op — in-place at
//     the wire level. The leaked old-A value now lives on the newly-
//     allocated record qubit indices listed in the registry entry.
//   - After the WHEN exits, the registry is back to its baseline size:
//     WhenScopeGarbage popped the record. The leaked physical qubits
//     themselves remain allocated (there is no gate-level uncompute to
//     return them to |0>), but the caller-visible audit trail has been
//     consumed.
//   - Because STURM_GARBAGE_REPORT was set, a single line on stderr of
//     the form
//       [STURM garbage] WHEN scope exit: 1 records leaked (tags: AND=1)
//     is printed when the WHEN scope exits. Depending on stdio
//     interleaving it may appear before or among the stdout sections.

static void dump_snapshot(const char* label) {
    const auto& snap = sturm::detail::garbage_registry::snapshot();
    std::printf("  %s: registry size = %zu\n", label, snap.size());
    for (std::size_t i = 0; i < snap.size(); ++i) {
        const auto& r = snap[i];
        const char* tag_name = "<?>";
        using T = sturm::detail::garbage_registry::source_op_tag;
        switch (r.tag) {
            case T::AND_ASSIGN:    tag_name = "AND_ASSIGN";    break;
            case T::OR_ASSIGN:     tag_name = "OR_ASSIGN";     break;
            case T::MUL_ASSIGN:    tag_name = "MUL_ASSIGN";    break;
            case T::DIV_ASSIGN:    tag_name = "DIV_ASSIGN";    break;
            case T::MOD_ASSIGN:    tag_name = "MOD_ASSIGN";    break;
            case T::MUL_UPPER_W:   tag_name = "MUL_UPPER_W";   break;
            case T::DIV_REMAINDER: tag_name = "DIV_REMAINDER"; break;
        }
        std::printf("    [%zu] op_id=%llu tag=%s ctrl=%d W=%d leaked_qubits={",
                    i,
                    static_cast<unsigned long long>(r.op_id),
                    tag_name,
                    r.ctrl_qubit,
                    r.W);
        for (std::size_t j = 0; j < r.qubit_indices.size(); ++j) {
            std::printf(" %d", r.qubit_indices[j]);
        }
        std::printf(" }\n");
    }
}

int main() {
    // Opt into sturm-njul's per-scope stderr diagnostic so the
    // WhenScopeGarbage consumer prints a line when the inner WHEN exits.
    // (The consumer always pops records; the env var only toggles the
    // diagnostic line.)
    setenv("STURM_GARBAGE_REPORT", "1", 1);

    constexpr uint32_t kNumQubits = 32;

    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND, kNumQubits);
    sturm_set_thread_context(ctx);

    // Give this demo a clean registry. clear() is documented as test /
    // demo only — never called by product code.
    sturm::detail::garbage_registry::clear();
    sturm::QubitPool::instance().reset_for_testing();

    // To exercise the sturm-h5it controlled-branch tail, both operands
    // and the WHEN control have to be on the QUANTUM path — not on the
    // classical short-circuit path that the operator's fast-path guard
    // (`qubits[0] < 0 && current_control == nullptr`) takes. Allocate
    // physical qubits for a, b, ctrl and attach them manually. This is
    // the same setup pattern the hermetic tests in
    // tests/backend/test_when_and_or_lossy.cpp use.
    constexpr std::size_t W = 3;
    int a_phys[W];
    int b_phys[W];
    for (std::size_t i = 0; i < W; ++i) {
        a_phys[i] = sturm::QubitPool::instance().allocate();
        b_phys[i] = sturm::QubitPool::instance().allocate();
    }
    int ctrl_phys = sturm::QubitPool::instance().allocate();

    std::printf("── before WHEN ────────────────────────────────────────\n");
    std::printf("  a allocated on physical qubits = { %d, %d, %d }\n",
                a_phys[0], a_phys[1], a_phys[2]);
    std::printf("  b allocated on physical qubits = { %d, %d, %d }\n",
                b_phys[0], b_phys[1], b_phys[2]);
    std::printf("  ctrl allocated on physical qubit = %d\n", ctrl_phys);
    dump_snapshot("baseline");

    {
        sturm::qint_t<W> a;
        sturm::qint_t<W> b;
        for (std::size_t i = 0; i < W; ++i) {
            a.qubits[i] = a_phys[i];
            b.qubits[i] = b_phys[i];
        }
        a.super_mask = (1ULL << W) - 1ULL;
        b.super_mask = (1ULL << W) - 1ULL;
        a.value = 6;   // classical shadow: 0b110
        b.value = 3;   // classical shadow: 0b011

        sturm::qbool ctrl = sturm::qbool::make_non_owning(ctrl_phys);
        ctrl.super_mask = 1ULL;
        ctrl.value      = 0;

        WHEN(ctrl) {
            // a &= b — under WHEN the operator takes the controlled tail:
            //   per-bit Fredkin between a's original qubits and a fresh
            //   res_idx[] register, then registers res_idx[] with the
            //   garbage_registry under source_op_tag::AND_ASSIGN. a's
            //   qubits[] indices are preserved across the op — the
            //   "in-place at the wire level" invariant sturm-h5it
            //   locks in.
            a &= b;

            std::printf("── inside WHEN, after a &= b ─────────────────────────\n");
            std::printf("  a.qubits = { %d, %d, %d }   (unchanged — in-place)\n",
                        a.qubits[0], a.qubits[1], a.qubits[2]);
            std::printf("  b.qubits = { %d, %d, %d }   (unchanged)\n",
                        b.qubits[0], b.qubits[1], b.qubits[2]);
            dump_snapshot("inside");
        }
        // WhenGuard has just destructed. Its WhenScopeGarbage member
        // popped every record added during the scope back to the pre-
        // WHEN baseline. Because STURM_GARBAGE_REPORT is set, a line
        // of the form
        //   [STURM garbage] WHEN scope exit: 1 records leaked (tags: AND=1)
        // is printed to stderr at this point.

        std::printf("── after WHEN ────────────────────────────────────────\n");
        dump_snapshot("post-scope");

        // Detach the qint handles from their physical qubits before the
        // local qints destruct, so we can release the qubits below
        // without double-release. The leaked record qubits (which the
        // sturm-njul consumer did NOT release) are tracked by the pool
        // itself; they remain allocated for the process lifetime here.
        for (std::size_t i = 0; i < W; ++i) {
            a.qubits[i] = -1;
            b.qubits[i] = -1;
        }
        ctrl.qubits[0] = -1;
        ctrl.owning_   = false;
    }

    // Clean up physical qubits we reserved for a, b, ctrl. The leaked
    // result register's qubits are NOT released here — that's the whole
    // point of the leak: they remain allocated and (in a real circuit)
    // entangled with old-A until a future uncomputation pass reclaims
    // them.
    for (std::size_t i = 0; i < W; ++i) {
        sturm::QubitPool::instance().release(a_phys[i]);
        sturm::QubitPool::instance().release(b_phys[i]);
    }
    sturm::QubitPool::instance().release(ctrl_phys);

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
    return 0;
}
