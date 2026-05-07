#pragma once
// qubit_pool.hpp — Unbounded ancilla pool (PRD §5.5 / G5).
// Header-only. Thread-safe via a single std::mutex.
//
// After sturm-zbzo (Frontend simpl. P2.a):
//   * No compile-time cap macro, sentinel constant, or abort message.
//   * No per-context cap argument; the pool grows on demand.
//   * `allocate()` and `acquire()` both grow on demand and never abort
//     — `acquire()` is kept as an alias so existing call sites (the
//     qbool / lossy_oop / lib_*_dsl primitives) keep compiling.
//   * SIMULATE-mode memory cost is the user's responsibility (PRD §5.5).

#include <mutex>
#include <vector>

namespace sturm {

class QubitPool {
public:
    // ── Singleton access ───────────────────────────────────────────────────
    static QubitPool& instance() {
        static QubitPool inst;
        return inst;
    }

    // ── Default constructor ────────────────────────────────────────────────
    // No cap argument; the pool grows on demand.
    QubitPool() = default;

    // ── Allocate one ancilla index ─────────────────────────────────────────
    // Returns a recycled index from the free-list if available, otherwise
    // hands out the next never-issued index. No cap; never throws.
    int allocate() {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!free_.empty()) {
            int idx = free_.back();
            free_.pop_back();
            ++in_use_;
            return idx;
        }
        int hw = high_water_;
        high_water_ = hw + 1;
        ++in_use_;
        return hw;
    }

    // ── acquire() — backwards-compatible alias (sturm-zbzo) ────────────────
    // Once enforced a hard cap and aborted on overflow; the cap is gone
    // (G5). The name remains so existing callers (qbool_ops.hpp,
    // lossy_oop.hpp, lib/*_dsl.hpp …) keep compiling without a mechanical
    // sweep. Every call now grows the pool on demand.
    int acquire() { return allocate(); }

    // ── Release an ancilla index back to the pool ──────────────────────────
    // TODO(backend): reset qubit to |0⟩ on the actual device before recycling.
    void release(int idx) {
        std::lock_guard<std::mutex> lk(mutex_);
        free_.push_back(idx);
        --in_use_;
    }

    // ── Diagnostics ────────────────────────────────────────────────────────
    int in_use() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return in_use_;
    }

    int high_water() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return high_water_;
    }

    // ── Testing support ────────────────────────────────────────────────────
    void reset_for_testing() {
        std::lock_guard<std::mutex> lk(mutex_);
        free_.clear();
        high_water_ = 0;
        in_use_     = 0;
    }

private:
    mutable std::mutex mutex_;
    std::vector<int>   free_;
    int                high_water_{0};
    int                in_use_{0};
};

} // namespace sturm
