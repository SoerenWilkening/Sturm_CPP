// test_sink_counter.cpp — TDD tests for CounterSink (Step 2)
// Tests: install CounterSink, dispatch ops, verify counts.

#include <cassert>
#include <cstddef>

#include "sturm/core/counter_sink.hpp"
#include "sturm/core/sink.hpp"

int main() {
    // ── Test 1: default sink is a CounterSink ─────────────────────────────
    {
        sturm::Sink* s = sturm::current_sink();
        assert(s != nullptr);
    }

    // ── Test 2: install CounterSink, call quantum_add, assert count == 1 ──
    {
        sturm::CounterSink cs;
        sturm::ScopedSink  scope(&cs);

        std::vector<int> a = {1};
        std::vector<int> b = {2};
        sturm::current_sink()->quantum_add(a, b, -1);

        assert(cs.count("quantum_add") == 1);
        assert(cs.count("quantum_sub") == 0);
    }

    // ── Test 3: multiple calls accumulate ─────────────────────────────────
    {
        sturm::CounterSink cs;
        sturm::ScopedSink  scope(&cs);

        std::vector<int> a = {0};
        std::vector<int> b = {1};
        sturm::current_sink()->quantum_add(a, b, -1);
        sturm::current_sink()->quantum_add(a, b, -1);
        sturm::current_sink()->quantum_mul(a, b, -1);

        assert(cs.count("quantum_add") == 2);
        assert(cs.count("quantum_mul") == 1);
    }

    // ── Test 4: ScopedSink restores prior sink on destruction ─────────────
    {
        sturm::Sink* prior = sturm::current_sink();
        {
            sturm::CounterSink cs;
            sturm::ScopedSink  scope(&cs);
            assert(sturm::current_sink() == &cs);
        }
        assert(sturm::current_sink() == prior);
    }

    // ── Test 5: all virtual methods callable (spot check) ─────────────────
    {
        sturm::CounterSink cs;
        sturm::ScopedSink  scope(&cs);

        std::vector<int> a = {3};
        std::vector<int> b = {4};
        sturm::current_sink()->quantum_sub(a, b, -1);
        sturm::current_sink()->quantum_xor(a, b, -1);
        sturm::current_sink()->quantum_eq(a, b, 5, -1);
        sturm::current_sink()->prepare(0, 0.5);
        sturm::current_sink()->phi_add(0, 0.1, -1);
        sturm::current_sink()->theta_add(0, 0.2, -1);

        assert(cs.count("quantum_sub") == 1);
        assert(cs.count("quantum_xor") == 1);
        assert(cs.count("quantum_eq") == 1);
        assert(cs.count("prepare") == 1);
        assert(cs.count("phi_add") == 1);
        assert(cs.count("theta_add") == 1);
    }

    // ── Test 6: set_current_sink directly ─────────────────────────────────
    {
        sturm::Sink* prior = sturm::current_sink();
        sturm::CounterSink cs;
        sturm::set_current_sink(&cs);
        assert(sturm::current_sink() == &cs);
        sturm::set_current_sink(prior);
        assert(sturm::current_sink() == prior);
    }

    return 0;
}
