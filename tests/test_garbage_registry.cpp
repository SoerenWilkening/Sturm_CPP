// test_garbage_registry.cpp — unit tests for sturm::detail::garbage_registry
// (sturm-h5it.5). This child adds the registry hook that Children 2/3/4 will
// call from the controlled branch of each lossy compound operator. Those
// children contribute integration tests that assert growth-by-one per op;
// here we lock down the registry's own API surface in isolation.
//
// Coverage:
//   1. Fresh state: snapshot() is empty after clear().
//   2. register_garbage copies indices and fills all fields correctly.
//   3. op_id is monotonic (starts at 0 after clear, increments by 1).
//   4. Multiple records accumulate in insertion order; clear() resets both
//      the list and the op_id counter.
//   5. All five source_op_tag values round-trip through the record.
//   6. W == 0 with nullptr indices is accepted (empty record).
//   7. Thread-local isolation: a separate thread has its own list.

#include "sturm/control/garbage_registry.hpp"

#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>

using sturm::detail::garbage_registry::clear;
using sturm::detail::garbage_registry::record;
using sturm::detail::garbage_registry::register_garbage;
using sturm::detail::garbage_registry::snapshot;
using sturm::detail::garbage_registry::source_op_tag;

static void test_empty_after_clear() {
    clear();
    assert(snapshot().empty() && "registry must be empty after clear()");
    std::puts("PASS: test_empty_after_clear");
}

static void test_single_register_fields() {
    clear();
    const int indices[3] = {7, 11, 13};
    register_garbage(source_op_tag::AND_ASSIGN,
                     /*ctrl_qubit=*/42,
                     /*W=*/3,
                     indices);
    const auto& v = snapshot();
    assert(v.size() == 1);
    const record& r = v.front();
    assert(r.op_id == 0);
    assert(r.ctrl_qubit == 42);
    assert(r.W == 3);
    assert(r.qubit_indices.size() == 3);
    assert(r.qubit_indices[0] == 7);
    assert(r.qubit_indices[1] == 11);
    assert(r.qubit_indices[2] == 13);
    assert(r.tag == source_op_tag::AND_ASSIGN);
    std::puts("PASS: test_single_register_fields");
}

static void test_op_id_monotonic() {
    clear();
    const int a[1] = {1};
    const int b[1] = {2};
    const int c[1] = {3};
    register_garbage(source_op_tag::OR_ASSIGN, -1, 1, a);
    register_garbage(source_op_tag::OR_ASSIGN, -1, 1, b);
    register_garbage(source_op_tag::OR_ASSIGN, -1, 1, c);
    const auto& v = snapshot();
    assert(v.size() == 3);
    assert(v[0].op_id == 0);
    assert(v[1].op_id == 1);
    assert(v[2].op_id == 2);
    // Insertion order preserved.
    assert(v[0].qubit_indices[0] == 1);
    assert(v[1].qubit_indices[0] == 2);
    assert(v[2].qubit_indices[0] == 3);
    std::puts("PASS: test_op_id_monotonic");
}

static void test_clear_resets_counter() {
    clear();
    const int idx[1] = {99};
    register_garbage(source_op_tag::MUL_ASSIGN, 5, 1, idx);
    register_garbage(source_op_tag::MUL_ASSIGN, 5, 1, idx);
    assert(snapshot().size() == 2);
    clear();
    assert(snapshot().empty());
    register_garbage(source_op_tag::MUL_ASSIGN, 5, 1, idx);
    const auto& v = snapshot();
    assert(v.size() == 1);
    assert(v.front().op_id == 0 && "op_id must reset after clear()");
    std::puts("PASS: test_clear_resets_counter");
}

static void test_all_tags_roundtrip() {
    clear();
    const int idx[1] = {0};
    const source_op_tag tags[] = {
        source_op_tag::AND_ASSIGN,
        source_op_tag::OR_ASSIGN,
        source_op_tag::MUL_ASSIGN,
        source_op_tag::DIV_ASSIGN,
        source_op_tag::MOD_ASSIGN,
    };
    for (auto t : tags) {
        register_garbage(t, 0, 1, idx);
    }
    const auto& v = snapshot();
    assert(v.size() == 5);
    for (std::size_t i = 0; i < 5; ++i) {
        assert(v[i].tag == tags[i]);
    }
    std::puts("PASS: test_all_tags_roundtrip");
}

static void test_zero_width_null_indices() {
    clear();
    register_garbage(source_op_tag::DIV_ASSIGN,
                     /*ctrl_qubit=*/3,
                     /*W=*/0,
                     /*qubit_indices=*/nullptr);
    const auto& v = snapshot();
    assert(v.size() == 1);
    assert(v.front().W == 0);
    assert(v.front().qubit_indices.empty());
    assert(v.front().ctrl_qubit == 3);
    assert(v.front().tag == source_op_tag::DIV_ASSIGN);
    std::puts("PASS: test_zero_width_null_indices");
}

static void test_thread_local_isolation() {
    clear();
    const int idx[1] = {77};
    register_garbage(source_op_tag::MOD_ASSIGN, 8, 1, idx);
    assert(snapshot().size() == 1);

    // On a separate thread the registry must be independent.
    std::size_t other_size_before = 999;
    std::size_t other_size_after  = 999;
    std::uint64_t other_op_id     = 999;
    std::thread t([&]() {
        other_size_before = snapshot().size();
        const int idx2[2] = {1, 2};
        register_garbage(source_op_tag::AND_ASSIGN, 0, 2, idx2);
        other_size_after = snapshot().size();
        other_op_id      = snapshot().back().op_id;
    });
    t.join();

    assert(other_size_before == 0 && "other thread must start empty");
    assert(other_size_after == 1);
    assert(other_op_id == 0 && "other thread's op_id counter is independent");

    // Main thread's state must be unchanged by the other thread's writes.
    assert(snapshot().size() == 1);
    assert(snapshot().front().qubit_indices[0] == 77);
    std::puts("PASS: test_thread_local_isolation");
}

int main() {
    test_empty_after_clear();
    test_single_register_fields();
    test_op_id_monotonic();
    test_clear_resets_counter();
    test_all_tags_roundtrip();
    test_zero_width_null_indices();
    test_thread_local_isolation();
    std::puts("All garbage_registry tests passed.");
    return 0;
}
