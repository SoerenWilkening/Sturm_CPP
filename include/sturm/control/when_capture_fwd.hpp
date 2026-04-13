#pragma once
// when_capture_fwd.hpp -- Thread-local callback for WHEN capture mechanism.
//
// Included by qint_core.hpp so the destructor can defer uncompute of
// intermediates in compound WHEN expressions.  Uses a function-pointer callback
// instead of a forward-declared struct to avoid incomplete-type dereferences
// (qint_core.hpp cannot include when_capture.hpp due to circular deps).
//
// WhenCapture (in when_capture.hpp) installs/clears this callback.

#ifdef STURM_BACKEND_ENABLED

#include <cstdint>
#include <array>

namespace sturm {
struct uncompute_op;  // forward declaration
}

namespace sturm::detail {

// Captured register data: all qubit indices + width, sufficient for the
// WhenCapture to reconstruct a qint_base view at uncompute time.
// This avoids the qint_core.hpp -> when_capture.hpp -> qint_base.hpp cycle.
struct CapturedRegister {
    std::array<int, 64> qubits;
    uint8_t width;
    int64_t value;
    uint64_t super_mask;
};

// Callback signature: (uncompute_op, register_data) -> void.
// When non-null, the active WhenCapture is capturing intermediates.
// qint_t's destructor calls this instead of running uncompute + release.
using when_capture_fn_t = void (*)(uncompute_op, const CapturedRegister&);

inline thread_local when_capture_fn_t when_capture_defer_fn = nullptr;

} // namespace sturm::detail

#endif // STURM_BACKEND_ENABLED
