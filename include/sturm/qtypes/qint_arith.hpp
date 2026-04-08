#pragma once
// qint_arith.hpp — Arithmetic operators for qint_t<Width>.
// Step 6 Module C, spec §3, Implementation Plan §6.
//
// Defines: operator+  operator-  operator*  operator/  operator%
//          unary operator-
//          compound assigns += -= *= /= %=
//          free pow(qint, qint) and pow(qint, int64_t)
//
// Each operator body is a thin call into dispatch_binary / dispatch_unary.
// Must be included after qint_core.hpp (via qint.hpp umbrella).

#include "sturm/qtypes/qint_core.hpp"
#include "sturm/core/dispatch.hpp"
#include "sturm/core/mask_ops.hpp"
#include "sturm/core/counter_sink.hpp"

#include <cstdint>
#include <vector>

namespace sturm {

// ── classical pow helper ──────────────────────────────────────────────────────
namespace detail {
inline int64_t classical_pow(int64_t base, int64_t exp) noexcept {
    if (exp < 0) return 0;
    int64_t result = 1;
    while (exp > 0) {
        if (exp & 1) result *= base;
        base *= base;
        exp >>= 1;
    }
    return result;
}
} // namespace detail

// ── operator+(qint_t, int64_t) — M21 pilot: uncompute-wired constant add ──────
//
// Returns a new qint_t<W> representing (a + c).  The result carries
// uncompute_op = ADD_CONST(c) so that when the result is destroyed its
// destructor emits the inverse (-= c) via the active BackendContext.
//
// Gate emission:
//   - If a is fully classical (super_mask == 0), returns a classical result
//     with no gates emitted (fast path).
//   - Otherwise calls qint_base::add_const(c, ctx) on a view of the result
//     register to emit the forward gate sequence via the backend context.
//
// Bennett discipline: `a` is never modified; the result is a fresh register.
//
// Only compiled when STURM_BACKEND_ENABLED is defined (backend builds).
//
// TODO(backend): replace add_const stub with a full Draper/ripple-carry
//                adder circuit when the library-op layer lands (M22+).

#ifdef STURM_BACKEND_ENABLED

template <std::size_t W>
qint_t<W> operator+(const qint_t<W>& a, int64_t c) {
    qint_t<W> result;
    result.value      = a.value + c;
    result.super_mask = a.super_mask;
    // Copy qubit indices (result shares the register structure for the stub).
    // TODO(backend): full Bennett requires allocating a fresh register and
    //                running the adder circuit into it; for the pilot test the
    //                stub emits gates on the same qubit set so the gate
    //                sequence is deterministic and invertible.
    result.qubits = a.qubits;

    if (a.super_mask != 0) {
        // Emit add_const gates via the active BackendContext (if any).
        if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
            qint_base view = result.as_qint_base();
            view.add_const(c, *ctx);
        }
    }

    // Stamp the uncompute op: destroying `result` will emit -= c.
    result.uncompute_ = uncompute_op::make_add_const(c);
    return result;
}

// Symmetric: operator+(int64_t, qint_t<W>)
template <std::size_t W>
qint_t<W> operator+(int64_t c, const qint_t<W>& a) {
    return a + c;
}

#endif  // STURM_BACKEND_ENABLED

// ── operator+ ─────────────────────────────────────────────────────────────────

template <std::size_t W>
qint_t<W> operator+(const qint_t<W>& a, const qint_t<W>& b) {
    return detail::dispatch_binary<qint_t<W>, qint_t<W>, qint_t<W>>(
        a, b,
        [](int64_t x, int64_t y) noexcept { return x + y; },
        [](uint64_t ma, uint64_t mb) noexcept {
            return detail::mask_addsub(ma, mb, static_cast<int>(W));
        },
        [](const qint_t<W>& aa, const qint_t<W>& bb,
           const qint_t<W>& /*out*/, int ctrl) {
            current_sink()->quantum_add(aa.qubits_vec(), bb.qubits_vec(), ctrl);
        });
}

// ── operator- (binary) ────────────────────────────────────────────────────────

template <std::size_t W>
qint_t<W> operator-(const qint_t<W>& a, const qint_t<W>& b) {
    return detail::dispatch_binary<qint_t<W>, qint_t<W>, qint_t<W>>(
        a, b,
        [](int64_t x, int64_t y) noexcept { return x - y; },
        [](uint64_t ma, uint64_t mb) noexcept {
            return detail::mask_addsub(ma, mb, static_cast<int>(W));
        },
        [](const qint_t<W>& aa, const qint_t<W>& bb,
           const qint_t<W>& /*out*/, int ctrl) {
            current_sink()->quantum_sub(aa.qubits_vec(), bb.qubits_vec(), ctrl);
        });
}

// ── unary operator- ───────────────────────────────────────────────────────────
// Computed as 0 - a. Dispatched as quantum_sub(empty_vec, a.qubits_vec, ctrl).

template <std::size_t W>
qint_t<W> operator-(const qint_t<W>& a) {
    return detail::dispatch_unary<qint_t<W>, qint_t<W>>(
        a,
        [](int64_t x) noexcept { return -x; },
        [](uint64_t ma) noexcept {
            return detail::mask_addsub(ma, 0ULL, static_cast<int>(W));
        },
        [](const qint_t<W>& aa, const qint_t<W>& /*out*/, int ctrl) {
            current_sink()->quantum_sub(std::vector<int>{}, aa.qubits_vec(), ctrl);
        });
}

// ── operator* ─────────────────────────────────────────────────────────────────

template <std::size_t W>
qint_t<W> operator*(const qint_t<W>& a, const qint_t<W>& b) {
    return detail::dispatch_binary<qint_t<W>, qint_t<W>, qint_t<W>>(
        a, b,
        [](int64_t x, int64_t y) noexcept { return x * y; },
        [](uint64_t ma, uint64_t mb) noexcept {
            return detail::mask_muldiv(ma, mb, static_cast<int>(W));
        },
        [](const qint_t<W>& aa, const qint_t<W>& bb,
           const qint_t<W>& /*out*/, int ctrl) {
            current_sink()->quantum_mul(aa.qubits_vec(), bb.qubits_vec(), ctrl);
        });
}

// ── operator/ ─────────────────────────────────────────────────────────────────

template <std::size_t W>
qint_t<W> operator/(const qint_t<W>& a, const qint_t<W>& b) {
    return detail::dispatch_binary<qint_t<W>, qint_t<W>, qint_t<W>>(
        a, b,
        [](int64_t x, int64_t y) noexcept { return y != 0 ? x / y : 0; },
        [](uint64_t ma, uint64_t mb) noexcept {
            return detail::mask_muldiv(ma, mb, static_cast<int>(W));
        },
        [](const qint_t<W>& aa, const qint_t<W>& bb,
           const qint_t<W>& /*out*/, int ctrl) {
            current_sink()->quantum_div(aa.qubits_vec(), bb.qubits_vec(), ctrl);
        });
}

// ── operator% ─────────────────────────────────────────────────────────────────

template <std::size_t W>
qint_t<W> operator%(const qint_t<W>& a, const qint_t<W>& b) {
    return detail::dispatch_binary<qint_t<W>, qint_t<W>, qint_t<W>>(
        a, b,
        [](int64_t x, int64_t y) noexcept { return y != 0 ? x % y : 0; },
        [](uint64_t ma, uint64_t mb) noexcept {
            return detail::mask_muldiv(ma, mb, static_cast<int>(W));
        },
        [](const qint_t<W>& aa, const qint_t<W>& bb,
           const qint_t<W>& /*out*/, int ctrl) {
            current_sink()->quantum_mod(aa.qubits_vec(), bb.qubits_vec(), ctrl);
        });
}

// ── pow (free function) ───────────────────────────────────────────────────────

template <std::size_t W>
qint_t<W> pow(const qint_t<W>& base, const qint_t<W>& exp) {
    return detail::dispatch_binary<qint_t<W>, qint_t<W>, qint_t<W>>(
        base, exp,
        [](int64_t b, int64_t e) noexcept { return detail::classical_pow(b, e); },
        [](uint64_t ma, uint64_t mb) noexcept {
            return detail::mask_muldiv(ma, mb, static_cast<int>(W));
        },
        [](const qint_t<W>& aa, const qint_t<W>& bb,
           const qint_t<W>& /*out*/, int ctrl) {
            current_sink()->quantum_pow(aa.qubits_vec(), bb.qubits_vec(), ctrl);
        });
}

template <std::size_t W>
qint_t<W> pow(const qint_t<W>& base, int64_t exp) {
    qint_t<W> e(exp);
    return pow(base, e);
}

// ── compound assign definitions ───────────────────────────────────────────────
// Defined here after the free binary operators are visible.

template <std::size_t W>
qint_t<W>& qint_t<W>::operator+=(const qint_t<W>& b) {
    *this = *this + b; return *this;
}

template <std::size_t W>
qint_t<W>& qint_t<W>::operator-=(const qint_t<W>& b) {
    *this = *this - b; return *this;
}

template <std::size_t W>
qint_t<W>& qint_t<W>::operator*=(const qint_t<W>& b) {
    *this = *this * b; return *this;
}

template <std::size_t W>
qint_t<W>& qint_t<W>::operator/=(const qint_t<W>& b) {
    *this = *this / b; return *this;
}

template <std::size_t W>
qint_t<W>& qint_t<W>::operator%=(const qint_t<W>& b) {
    *this = *this % b; return *this;
}

} // namespace sturm
