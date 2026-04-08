// test_sink_recording.cpp — TDD tests for RecordingSink (Step 2)
// Tests: ScopedSink RAII, Record contents, clear().

#include <cassert>
#include <string>
#include <vector>

#include "sturm/core/counter_sink.hpp"    // provides current_sink / set_current_sink
#include "sturm/core/recording_sink.hpp"
#include "sturm/core/sink.hpp"

int main() {
    // ── Test 1: RecordingSink starts empty ────────────────────────────────
    {
        sturm::RecordingSink rs;
        assert(rs.records().empty());
    }

    // ── Test 2: ScopedSink installs and records ───────────────────────────
    {
        sturm::RecordingSink rs;
        {
            sturm::ScopedSink scope(&rs);
            std::vector<int> a = {1, 2};
            std::vector<int> b = {3};
            sturm::current_sink()->quantum_add(a, b, -1);
        }
        assert(rs.records().size() == 1);
        const sturm::Record& r = rs.records()[0];
        assert(r.op == "quantum_add");
        assert(r.qubit_groups.size() == 2);
        assert(r.qubit_groups[0] == std::vector<int>({1, 2}));
        assert(r.qubit_groups[1] == std::vector<int>({3}));
        assert(r.control == -1);
    }

    // ── Test 3: ScopedSink destructor restores prior sink ─────────────────
    {
        sturm::Sink* prior = sturm::current_sink();
        {
            sturm::RecordingSink rs;
            sturm::ScopedSink    scope(&rs);
            assert(sturm::current_sink() == &rs);
        }
        assert(sturm::current_sink() == prior);
    }

    // ── Test 4: multiple ops recorded in order ────────────────────────────
    {
        sturm::RecordingSink rs;
        sturm::ScopedSink    scope(&rs);

        std::vector<int> a = {0};
        std::vector<int> b = {1};
        sturm::current_sink()->quantum_xor(a, b, -1);
        sturm::current_sink()->quantum_mul(a, b, 7);

        assert(rs.records().size() == 2);
        assert(rs.records()[0].op == "quantum_xor");
        assert(rs.records()[0].control == -1);
        assert(rs.records()[1].op == "quantum_mul");
        assert(rs.records()[1].control == 7);
    }

    // ── Test 5: clear() empties the record list ───────────────────────────
    {
        sturm::RecordingSink rs;
        sturm::ScopedSink    scope(&rs);

        std::vector<int> a = {0};
        std::vector<int> b = {1};
        sturm::current_sink()->quantum_sub(a, b, -1);
        assert(rs.records().size() == 1);
        rs.clear();
        assert(rs.records().empty());
    }

    // ── Test 6: prepare records scalars ──────────────────────────────────
    {
        sturm::RecordingSink rs;
        sturm::ScopedSink    scope(&rs);

        sturm::current_sink()->prepare(5, 0.75);

        assert(rs.records().size() == 1);
        assert(rs.records()[0].op == "prepare");
        assert(rs.records()[0].scalars.size() == 1);
        assert(rs.records()[0].scalars[0] == 0.75);
        // qubit_groups[0] == {5}
        assert(rs.records()[0].qubit_groups.size() == 1);
        assert(rs.records()[0].qubit_groups[0][0] == 5);
        assert(rs.records()[0].control == -1);
    }

    // ── Test 7: phi_add and theta_add record scalar and qubit ────────────
    {
        sturm::RecordingSink rs;
        sturm::ScopedSink    scope(&rs);

        sturm::current_sink()->phi_add(3, 0.25, -1);
        sturm::current_sink()->theta_add(4, 0.5, 2);

        assert(rs.records().size() == 2);
        assert(rs.records()[0].op == "phi_add");
        assert(rs.records()[0].scalars[0] == 0.25);
        assert(rs.records()[0].qubit_groups[0][0] == 3);
        assert(rs.records()[0].control == -1);

        assert(rs.records()[1].op == "theta_add");
        assert(rs.records()[1].scalars[0] == 0.5);
        assert(rs.records()[1].qubit_groups[0][0] == 4);
        assert(rs.records()[1].control == 2);
    }

    // ── Test 8: quantum_not (unary) ───────────────────────────────────────
    {
        sturm::RecordingSink rs;
        sturm::ScopedSink    scope(&rs);

        std::vector<int> a = {10, 11};
        sturm::current_sink()->quantum_not(a, -1);

        assert(rs.records().size() == 1);
        assert(rs.records()[0].op == "quantum_not");
        assert(rs.records()[0].qubit_groups.size() == 1);
        assert(rs.records()[0].qubit_groups[0] == a);
        assert(rs.records()[0].control == -1);
    }

    // ── Test 9: quantum_eq with result qubit ──────────────────────────────
    {
        sturm::RecordingSink rs;
        sturm::ScopedSink    scope(&rs);

        std::vector<int> a = {1};
        std::vector<int> b = {2};
        int result_qubit = 9;
        sturm::current_sink()->quantum_eq(a, b, result_qubit, -1);

        assert(rs.records().size() == 1);
        assert(rs.records()[0].op == "quantum_eq");
        // qubit_groups: [a, b, {result_qubit}]
        assert(rs.records()[0].qubit_groups.size() == 3);
        assert(rs.records()[0].qubit_groups[2][0] == result_qubit);
        assert(rs.records()[0].control == -1);
    }

    return 0;
}
