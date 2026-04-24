#pragma once
// garbage_registry.hpp — thread-local discoverable-leak registry for controlled
// lossy compound assignments (sturm-h5it, Child 5).
//
// Purpose:
//   qint_t::operator&=, |=, *=, /=, %= are lossy on the left-hand operand.
//   Outside WHEN, they fake in-place semantics with a release(old_A) + pointer
//   relabel tail. Inside WHEN(ctrl), that relabel is wrong: it unconditionally
//   zeroes A in the ctrl=|0> branch (Children 2/3/4 of this epic replace it
//   with a per-bit controlled-SWAP, deliberately *leaking* the result
//   register because full uncomputation is impossible without extra state).
//
//   Rather than anonymously leak those W qubits, this registry records each
//   leak as a discoverable entry so a future transpiler pass can locate the
//   garbage registers and emit proper uncomputation. No consumer this session;
//   the registry just accumulates.
//
// API (namespace sturm::detail::garbage_registry):
//   enum class source_op_tag { AND_ASSIGN, OR_ASSIGN, MUL_ASSIGN,
//                              DIV_ASSIGN, MOD_ASSIGN };
//   struct record { uint64_t op_id; int ctrl_qubit; int W;
//                   std::vector<int> qubit_indices; source_op_tag tag; };
//   void register_garbage(source_op_tag tag, int ctrl_qubit, int W,
//                         const int* qubit_indices);
//   const std::vector<record>& snapshot() noexcept;
//   void clear() noexcept;   // test isolation only — never in product code.
//
// Thread-local: each thread has its own list and op-id counter. The ctrl_qubit
// is captured by the caller from detail::current_control_qubit at register
// time; we accept it as a parameter to keep this header free of a when_fwd.hpp
// include and symmetric with the other fields (see spec bullet).

#include <cstdint>
#include <vector>

namespace sturm::detail::garbage_registry {

// ── source_op_tag ────────────────────────────────────────────────────────────
// Identifies which lossy compound operator produced a garbage register.
enum class source_op_tag : unsigned {
    AND_ASSIGN = 0,
    OR_ASSIGN  = 1,
    MUL_ASSIGN = 2,
    DIV_ASSIGN = 3,
    MOD_ASSIGN = 4,
};

// ── record ───────────────────────────────────────────────────────────────────
// One entry per controlled lossy op. The qubit_indices vector holds the W
// leaked result-register indices (res_idx[0..W-1]) in bit-order.
struct record {
    std::uint64_t     op_id;           // monotonic per-thread sequence id
    int               ctrl_qubit;      // copied from current_control_qubit
    int               W;               // width; qubit_indices.size() == W
    std::vector<int>  qubit_indices;   // the leaked register
    source_op_tag     tag;             // which operator produced it
};

// ── internal TLS (inline vars — header-only, per-thread) ────────────────────
namespace _tls {
    inline thread_local std::vector<record> records;
    inline thread_local std::uint64_t       next_op_id = 0;
} // namespace _tls

// ── register_garbage ─────────────────────────────────────────────────────────
// Push a new record onto the thread-local list. Assigns a fresh op_id. The
// qubit_indices parameter is an array of length W; its contents are copied
// into the record's vector. Passing a null pointer with W > 0 is a precondition
// violation (caller's responsibility). W == 0 and qubit_indices == nullptr is
// tolerated (empty record).
inline void register_garbage(source_op_tag tag,
                             int ctrl_qubit,
                             int W,
                             const int* qubit_indices) noexcept {
    record r;
    r.op_id      = _tls::next_op_id++;
    r.ctrl_qubit = ctrl_qubit;
    r.W          = W;
    r.tag        = tag;
    if (W > 0 && qubit_indices != nullptr) {
        r.qubit_indices.assign(qubit_indices, qubit_indices + W);
    }
    _tls::records.push_back(std::move(r));
}

// ── snapshot ─────────────────────────────────────────────────────────────────
// Const view over the thread-local record list. Intended for tests now and
// for a future transpiler pass consuming the leak inventory.
inline const std::vector<record>& snapshot() noexcept {
    return _tls::records;
}

// ── clear ────────────────────────────────────────────────────────────────────
// Test isolation only. Resets the per-thread list AND the monotonic counter
// so tests can make exact-value assertions about op_id. Never called by
// product code.
inline void clear() noexcept {
    _tls::records.clear();
    _tls::next_op_id = 0;
}

} // namespace sturm::detail::garbage_registry
