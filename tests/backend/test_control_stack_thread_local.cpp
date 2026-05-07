// test_control_stack_thread_local.cpp — M12: thread-local control stack tests.
// TDD: written before implementation.
//
// Tests:
//   1. two contexts on different threads have independent stacks

#include "sturm/core/context.hpp"
#include "sturm/core/control_stack.hpp"

#include <cassert>
#include <cstdint>
#include <thread>

// ── Test 1: two threads get independent control stacks ───────────────────────

static void test_thread_local_independent() {
    // Main thread: create a context, install it, push a control.
    sturm_backend_context_t* ctx_main =
        sturm_backend_create(STURM_MODE_COUNT_ONLY);
    assert(ctx_main != nullptr);
    sturm_set_thread_context(ctx_main);

    sturm::current_control_stack().push_control(42u);
    assert(sturm::current_control_stack().depth() == 1u);
    assert(sturm::current_control_stack().top()   == 42u);

    // Spawn a second thread that installs its own fresh context.
    bool thread_ok = false;
    std::thread worker([&thread_ok]() {
        sturm_backend_context_t* ctx_worker =
            sturm_backend_create(STURM_MODE_COUNT_ONLY);
        assert(ctx_worker != nullptr);
        sturm_set_thread_context(ctx_worker);

        // Worker thread should see an empty stack (independent from main).
        assert(sturm::current_control_stack().depth() == 0u);

        // Push a different qubit on the worker's stack.
        sturm::current_control_stack().push_control(99u);
        assert(sturm::current_control_stack().depth() == 1u);
        assert(sturm::current_control_stack().top()   == 99u);

        sturm_backend_destroy(ctx_worker);
        thread_ok = true;
    });
    worker.join();

    // Main thread's stack must be unaffected by the worker.
    assert(sturm::current_control_stack().depth() == 1u);
    assert(sturm::current_control_stack().top()   == 42u);

    // Cleanup.
    sturm::current_control_stack().pop_control();
    assert(sturm::current_control_stack().depth() == 0u);

    sturm_backend_destroy(ctx_main);
    assert(thread_ok);
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    test_thread_local_independent();
    return 0;
}
