#pragma once
// qubit_pool.hpp — Bounded ancilla pool (Step 1, spec §1.1)
// Header-only singleton. Capacity = STURM_ANCILLA_CAPACITY (default 256).
// Thread-safe via a single std::mutex.

#include <atomic>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace sturm {

class QubitPool {
public:
    // ── Compile-time capacity ──────────────────────────────────────────────
    static constexpr int kCapacity = STURM_ANCILLA_CAPACITY;

    // ── Singleton access ───────────────────────────────────────────────────
    static QubitPool& instance() {
        static QubitPool inst;
        return inst;
    }

    // ── Allocate one ancilla index ─────────────────────────────────────────
    // Returns a recycled index from the free-list if available, otherwise
    // hands out the next never-issued index. Throws if pool is exhausted.
    int allocate() {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!free_.empty()) {
            int idx = free_.back();
            free_.pop_back();
            ++in_use_;
            return idx;
        }
        int hw = high_water_;
        if (hw >= kCapacity) {
            throw std::runtime_error("ancilla pool exhausted");
        }
        high_water_ = hw + 1;
        ++in_use_;
        return hw;
    }

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

    static constexpr int capacity() { return kCapacity; }

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
    QubitPool() = default;

    mutable std::mutex mutex_;
    std::vector<int>   free_;
    int                high_water_{0};
    int                in_use_{0};
};

} // namespace sturm
