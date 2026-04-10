// control_stack.cpp — M12: thread-local ControlStack accessor implementation.
//
// current_control_stack() retrieves the calling thread's active BackendContext
// (via sturm_get_thread_context()) and returns a reference to its embedded
// ControlStack member.
//
// Requires that a context has been installed on the calling thread before
// current_control_stack() is called.
//
// Target: <40 LoC.

#include "sturm/core/control_stack.hpp"
#include "sturm/core/context.hpp"

#include <cassert>

namespace sturm {

ControlStack& current_control_stack() {
    sturm_backend_context_t* ctx = sturm_get_thread_context();
    assert(ctx && "current_control_stack(): no BackendContext installed on this thread");
    return ctx->control_stack;
}

} // namespace sturm
