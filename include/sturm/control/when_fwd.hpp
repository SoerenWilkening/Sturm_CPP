#pragma once
// when_fwd.hpp — Forward declaration of the thread-local current_control pointer.
// Step 5, spec §6, Implementation Plan §5.
//
// Included by dispatch.hpp (Step 5) and when.hpp (Step 7) to avoid cycles.
// when.hpp provides WhenGuard and the WHEN macro; this file provides only the
// TLS variable so dispatch.hpp can read it without including when.hpp.

// qbool must be fully defined before we can declare a pointer to it.
#include "sturm/qtypes/qbool.hpp"

namespace sturm::detail {

// Thread-local pointer to the currently-active WHEN guard condition.
// nullptr = no active WHEN; non-null = pointer to the qbool flag expression.
// Set/restored exclusively by WhenGuard (when.hpp).
inline thread_local qbool* current_control = nullptr;

} // namespace sturm::detail
