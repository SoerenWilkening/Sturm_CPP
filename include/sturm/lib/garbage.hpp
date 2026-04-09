// garbage.hpp — M5 (PRD v2): displaced-register garbage tracker and uncompute
// scheduler.
//
// GarbageManager:
//   Tracks registers that have been displaced by a relabel-move (PRD v2 §7).
//   After an uncontrolled move_result, the old destination register's qubit
//   indices are handed here.  The manager stores them along with an "uncompute
//   function" (a callable that reverses the operation that produced the result).
//   When flush() is called the manager runs each uncompute in reverse
//   registration order and then returns the qubits to the AncillaManager.
//
// Typical lifecycle (PRD v2 §7, impl plan M5):
//   1. Caller computes out-of-place: c = a | b   (c is the fresh result reg).
//   2. Caller calls move_result: dest and c swap their index arrays.
//      The old dest qubits (now held by c's wrapper) carry entangled history.
//   3. Caller calls gc.track(old_dest_idxs, n, uncompute_fn).
//   4. When uncomputing is appropriate the caller calls gc.flush().
//      flush() runs each registered uncompute_fn in reverse, then calls
//      mgr.free_ancilla() for each qubit.
//
// Integration with AncillaManager:
//   GarbageManager takes a reference to AncillaManager at construction time.
//   free_ancilla() is called on each displaced qubit after its uncompute_fn
//   runs and the qubit is guaranteed to be back in |0⟩.
//
// Design constraints:
//   - uncompute_fn is stored as std::function<void()> (captures the SimState
//     reference and all operand qubit indices via closure).
//   - GarbageManager is not copyable (holds references).
//   - Displaced qubit indices are stored by value (copied when tracked).
//
// Target: <250 LoC (impl plan M5).

#pragma once

#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <vector>

namespace sturm {
namespace v2 {

// ── GarbageManager ────────────────────────────────────────────────────────────

class GarbageManager {
public:
    // Construct with references to the SimState and AncillaManager.
    // Both must outlive this GarbageManager.
    explicit GarbageManager(SimState& s, AncillaManager& mgr)
        : s_(s), mgr_(mgr) {}

    // Non-copyable, non-movable (holds references).
    GarbageManager(const GarbageManager&)            = delete;
    GarbageManager& operator=(const GarbageManager&) = delete;

    // ── track ─────────────────────────────────────────────────────────────────
    //
    // Register a displaced register for future uncomputing.
    //
    // Parameters:
    //   displaced_idxs — array of qubit indices belonging to the displaced
    //                    register.  Copied into the entry.
    //   n              — number of qubits.
    //   uncompute_fn   — callable that, when invoked, emits the gate sequence
    //                    that reverses the operation that produced the result
    //                    and leaves displaced_idxs qubits in |0⟩.
    //                    May be empty (nullptr) if the displaced register is
    //                    already guaranteed to be |0⟩ (e.g. after a direct
    //                    copy into a fresh ancilla with no further entanglement).
    //
    // After track() returns, the caller must NOT use displaced_idxs again for
    // gate emission — ownership of those qubit indices has been transferred to
    // the GarbageManager.
    void track(const uint32_t* displaced_idxs, uint32_t n,
               std::function<void()> uncompute_fn) {
        Entry e;
        e.qubits.assign(displaced_idxs, displaced_idxs + n);
        e.uncompute_fn = std::move(uncompute_fn);
        entries_.push_back(std::move(e));
    }

    // ── flush ─────────────────────────────────────────────────────────────────
    //
    // Run all pending uncompute functions in reverse registration order, then
    // free the qubits back to the AncillaManager.
    //
    // After flush() returns the GarbageManager holds no pending entries and
    // the AncillaManager's in-use count is reduced by the total number of
    // flushed qubits.
    //
    // Throws std::runtime_error (via AncillaManager::free_ancilla) if any
    // qubit is not in |0⟩ after its uncompute_fn executes — this indicates a
    // bug in the registered uncompute function.
    void flush() {
        // Process in reverse registration order (LIFO uncompute).
        for (int i = static_cast<int>(entries_.size()) - 1; i >= 0; --i) {
            Entry& e = entries_[static_cast<uint32_t>(i)];
            // Run the uncompute function (may be empty for trivially-zero regs).
            if (e.uncompute_fn) {
                e.uncompute_fn();
            }
            // Return each qubit to the ancilla pool.
            for (uint32_t q : e.qubits) {
                mgr_.free_ancilla(q);
            }
        }
        entries_.clear();
    }

    // ── pending_count ─────────────────────────────────────────────────────────
    //
    // Returns the number of displacement entries not yet flushed.
    uint32_t pending_count() const {
        return static_cast<uint32_t>(entries_.size());
    }

    // ── total_pending_qubits ──────────────────────────────────────────────────
    //
    // Returns the total number of qubit indices tracked but not yet flushed.
    uint32_t total_pending_qubits() const {
        uint32_t total = 0;
        for (const auto& e : entries_) {
            total += static_cast<uint32_t>(e.qubits.size());
        }
        return total;
    }

private:
    struct Entry {
        std::vector<uint32_t>  qubits;        // displaced qubit indices
        std::function<void()>  uncompute_fn;  // reverses the producing op
    };

    SimState&              s_;
    AncillaManager&        mgr_;
    std::vector<Entry>     entries_;
};

} // namespace v2
} // namespace sturm
