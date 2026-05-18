// classical_replay.hpp — sturm-scin: shared APPEND+classical-replay helper
// for the modular-arithmetic DSL test suite.
//
// Every `lib_*_mod_dsl` algorithm is built entirely from classical-reversible
// primitives (X / CX / CCX); running them under STURM_MODE_SIMULATE forces
// the orkan backend to materialise a 2^n_orkan amplitude state vector and
// the per-case read_reg() to scan all 2^n_orkan amplitudes for the unique
// non-zero entry. For W=2 exhaustive sweeps (14 cases) and W=3 random
// sweeps (50 cases) this costs minutes-to-hours per test target.
//
// The fast equivalent — first published in
// `tests/lib/test_mul_mod_dsl.cpp` Beat 2.4 (sturm-kubb.4) — is:
//   1. Install a STURM_MODE_APPEND context. execute_gate() records each
//      primitive into ctx->ir without touching a statevector.
//   2. Call the algorithm with non-owning qbool / BitProxy wrappers over
//      pre-allocated QubitPool indices.
//   3. Snapshot QubitPool::high_water() before tearing down the context
//      to size a classical std::vector<uint8_t> bit-vector.
//   4. Seed the bit-vector with the classical input register values at
//      the qubit indices the test pre-allocated.
//   5. Replay each GateRecord as a deterministic bit-flip program:
//        X    : flip target,
//        CX   : if ctrl  bit set then flip target,
//        CCX  : if both control bits set then flip target,
//        else : std::abort() (the harness is only honest for
//               classical-reversible primitives; any rotation / Hadamard /
//               other primitive in a modular-arith path breaks the
//               trace-as-classical-reference invariant and we want to
//               know loudly).
//
// Per-case cost drops from O(2^n_orkan * |IR|) to O(|IR|). On the W=2
// mul-mod sweep the test wall-clock drops from ~1622 s to a few seconds.
//
// Migration note: the equivalent SIMULATE-mode helpers (SimCtx / read_reg)
// remain in the individual test files so each algorithm retains at least
// one SIMULATE smoke case for end-to-end statevector coverage; only the
// exhaustive / sweep cases migrate to this header.

#ifndef STURM_TESTS_LIB_CLASSICAL_REPLAY_HPP
#define STURM_TESTS_LIB_CLASSICAL_REPLAY_HPP

#include "sturm/backend/ir.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/gate_kind.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace sturm::test_helpers {

// ── apply_gate_classical ────────────────────────────────────────────────────
//
// Apply one captured GateRecord to a classical bit-vector.  Supports only
// X / CX / CCX (the modular-arith DSL is built entirely from these); any
// other gate kind aborts loudly so a future refactor that adds a
// non-classical primitive to a mod-arith path cannot silently invalidate
// the trace harness's classical-reversible reference contract.

inline void apply_gate_classical(std::vector<uint8_t>& bits,
                                 const sturm::GateRecord& rec) {
    switch (rec.kind) {
    case STURM_GATE_X:
        bits[rec.qubits[0]] ^= 1u;
        break;
    case STURM_GATE_CX:
        if (bits[rec.qubits[0]]) bits[rec.qubits[1]] ^= 1u;
        break;
    case STURM_GATE_CCX:
        if (bits[rec.qubits[0]] && bits[rec.qubits[1]])
            bits[rec.qubits[2]] ^= 1u;
        break;
    default:
        std::fprintf(stderr,
                     "classical_replay: unsupported gate kind %d "
                     "(non-classical primitive in modular-arith path)\n",
                     static_cast<int>(rec.kind));
        std::abort();
    }
}

// ── read_reg_classical ──────────────────────────────────────────────────────
//
// Decode an n-bit classical register out of the bit-vector by ORing the
// bits at the qubit-index slots into a single uint32_t.  Returns 0 for any
// slot whose qubit index is negative (the qint convention for "not
// allocated"), matching the SIMULATE-mode read_reg semantics.

inline uint32_t read_reg_classical(const std::vector<uint8_t>& bits,
                                   const int* qi, std::size_t n) {
    uint32_t v = 0u;
    for (std::size_t k = 0; k < n; ++k) {
        if (qi[k] >= 0 && bits[static_cast<std::size_t>(qi[k])])
            v |= (1u << k);
    }
    return v;
}

// ── AppendContext ───────────────────────────────────────────────────────────
//
// RAII wrapper installing a fresh STURM_MODE_APPEND context as the calling
// thread's active context; destructor restores the previously-installed
// context and destroys the APPEND one.  Use `ctx()` to access ctx->ir for
// the replay step.

class AppendContext {
public:
    AppendContext() {
        ctx_ = sturm_backend_create(STURM_MODE_APPEND);
        assert(ctx_);
        prev_ = sturm_get_thread_context();
        sturm_set_thread_context(ctx_);
    }
    ~AppendContext() {
        sturm_set_thread_context(prev_);
        sturm_backend_destroy(ctx_);
    }
    AppendContext(const AppendContext&)            = delete;
    AppendContext& operator=(const AppendContext&) = delete;

    sturm_backend_context_t* ctx() const noexcept { return ctx_; }

private:
    sturm_backend_context_t* ctx_  = nullptr;
    sturm_backend_context_t* prev_ = nullptr;
};

// ── replay_ir ───────────────────────────────────────────────────────────────
//
// Apply every GateRecord in the captured IR to the classical bit-vector,
// in order.  The bit-vector must be sized to at least
// QubitPool::high_water() and seeded with the classical input register
// values before this call.

inline void replay_ir(const sturm::GateIR& ir,
                      std::vector<uint8_t>& bits) {
    for (std::size_t i = 0; i < ir.size(); ++i) {
        apply_gate_classical(bits, ir.at(i));
    }
}

} // namespace sturm::test_helpers

#endif // STURM_TESTS_LIB_CLASSICAL_REPLAY_HPP
