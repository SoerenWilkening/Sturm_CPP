#pragma once
// when_fwd.hpp — Forward declaration of the thread-local current_control pointer.
// Step 5, spec §6, Implementation Plan §5.
//
// Included by dispatch.hpp (Step 5) and when.hpp (Step 7) to avoid cycles.
// when.hpp provides WhenGuard and the WHEN macro; this file provides only the
// TLS variable so dispatch.hpp can read it without including when.hpp.
//
// M5: qbool now inherits qint_t<1>, so qbool.hpp includes qint_core.hpp, which
// includes when_fwd.hpp. To break the cycle, we use a forward declaration of
// qbool here instead of including qbool.hpp — a pointer-to-qbool only needs the
// forward declaration.

namespace sturm {
// Forward declaration — full definition in qbool.hpp.
class qbool;
} // namespace sturm

namespace sturm::detail {

// Thread-local pointer to the currently-active WHEN guard condition.
// nullptr = no active WHEN; non-null = pointer to the qbool flag expression.
// Set/restored exclusively by WhenGuard (when.hpp).
inline thread_local qbool* current_control = nullptr;

// Cached qubit index of current_control, kept in sync by WhenGuard.
// Avoids dereferencing the qbool* in headers where qbool is incomplete.
inline thread_local int current_control_qubit = -1;

} // namespace sturm::detail
