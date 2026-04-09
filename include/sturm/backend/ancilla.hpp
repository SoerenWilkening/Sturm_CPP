// ancilla.hpp — M1 (PRD v2): ancilla qubit lifecycle management.
//
// AncillaManager borrows scratch qubits from a fixed-size SimState and
// enforces the |0⟩ contract required by reversible computing:
//
//   uint32_t allocate_ancilla()  — hand out the next free qubit index;
//                                  always returns a qubit that is in |0⟩.
//   void free_ancilla(uint32_t)  — return a qubit; throws std::runtime_error
//                                  if the qubit is not in |0⟩ (dirty-free
//                                  check) or has already been freed (double-
//                                  free check).
//
// Design (PRD v2 §3.3, impl plan M1):
//   - The manager holds a reference to a SimState (must outlive the manager).
//   - Qubit indices are assigned sequentially from 0 up to
//     SimState::num_qubits().  Freed qubits go back on a free-list and may
//     be reused.
//   - The |0⟩ check is always enabled (not only in debug mode); it is
//     cheap for the small qubit counts the backend targets.
//
// Target: <150 LoC (impl plan M1).

#pragma once

#include "sturm/backend/state.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace sturm {
namespace v2 {

// ── AncillaManager ───────────────────────────────────────────────────────────

class AncillaManager {
public:
    // Construct with a reference to the SimState that holds the physical
    // qubits.  The SimState must already have been allocate()'d and must
    // outlive this manager.
    explicit AncillaManager(SimState& sim) : sim_(sim) {}

    // Non-copyable, non-movable (holds a reference).
    AncillaManager(const AncillaManager&)            = delete;
    AncillaManager& operator=(const AncillaManager&) = delete;

    // ── allocate_ancilla ──────────────────────────────────────────────────────
    //
    // Return the index of a free qubit that is guaranteed to be in |0⟩.
    // Throws std::runtime_error if all qubits are in use.
    uint32_t allocate_ancilla() {
        uint32_t q;
        if (!free_list_.empty()) {
            q = free_list_.back();
            free_list_.pop_back();
        } else {
            if (next_idx_ >= sim_.num_qubits()) {
                throw std::runtime_error(
                    "AncillaManager: all qubits in use; cannot allocate ancilla");
            }
            q = next_idx_++;
        }
        in_use_.push_back(q);
        // Guarantee: qubit must be |0⟩.  For new qubits from a freshly
        // allocated SimState this is trivially true.  For recycled qubits the
        // free_ancilla() check ensures the previous owner cleaned up.
        assert(is_zero(q) && "allocate_ancilla: recycled qubit is not |0>");
        return q;
    }

    // ── free_ancilla ──────────────────────────────────────────────────────────
    //
    // Return qubit q to the pool.
    // Throws std::runtime_error if:
    //   - q is not currently allocated (double-free).
    //   - q is not in |0⟩ (dirty-free: the caller failed to uncompute).
    void free_ancilla(uint32_t q) {
        // Double-free check: q must be in the in-use list.
        auto it = find_in_use(q);
        if (it == in_use_.end()) {
            throw std::runtime_error(
                "AncillaManager::free_ancilla: qubit not allocated (double-free?)");
        }
        // Dirty-free check: qubit must be |0⟩.
        if (!is_zero(q)) {
            throw std::runtime_error(
                "AncillaManager::free_ancilla: qubit is not |0> (leaked entanglement)");
        }
        in_use_.erase(it);
        free_list_.push_back(q);
    }

    // ── Diagnostics ───────────────────────────────────────────────────────────

    uint32_t num_in_use() const { return static_cast<uint32_t>(in_use_.size()); }

private:
    // Check whether qubit q is in the |0⟩ state (all amplitudes with bit q
    // set to 1 are negligibly small).
    static constexpr double kZeroTol = 1e-9;

    bool is_zero(uint32_t q) const {
        uint64_t dim  = uint64_t{1} << sim_.num_qubits();
        uint64_t mask = uint64_t{1} << q;
        for (uint64_t i = 0; i < dim; ++i) {
            if ((i & mask) != 0u) {
                if (std::abs(sim_.amplitude(i)) > kZeroTol) return false;
            }
        }
        return true;
    }

    std::vector<uint32_t>::iterator find_in_use(uint32_t q) {
        for (auto it = in_use_.begin(); it != in_use_.end(); ++it) {
            if (*it == q) return it;
        }
        return in_use_.end();
    }

    SimState&             sim_;
    uint32_t              next_idx_{0};   // next never-issued qubit index
    std::vector<uint32_t> in_use_;        // currently allocated qubits
    std::vector<uint32_t> free_list_;     // returned qubits available for reuse
};

} // namespace v2
} // namespace sturm
