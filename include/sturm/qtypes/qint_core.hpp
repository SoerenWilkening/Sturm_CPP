#pragma once
// qint_core.hpp — Core class definition for qint_t<Width>.
// Step 6 Module B, spec §3, Implementation Plan §6.
//
// Contains: data members, all constructors, assignment operators, conversions,
// qubits_vec() helper, operator declarations (bodies in arith/bitwise/compare),
// PhiProxy and ThetaProxy nested types.
//
// Arithmetic/bitwise/compare operator definitions are in separate headers
// included via qint.hpp umbrella. The compound-assign member bodies are defined
// in qint_arith.hpp after the free operator+ etc. are visible.

#include "sturm/qtypes/qint_fwd.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/counter_sink.hpp"  // current_sink()
#include "sturm/control/when_fwd.hpp"   // detail::current_control for proxies

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sturm {

template <std::size_t Width>
class qint_t {
    static_assert(Width >= 1 && Width <= 64,
                  "qint_t Width must be in [1, 64]");

public:
    using value_type = int64_t;
    using mask_type  = uint64_t;

    // ── Data members (public for direct test inspection and dispatch helpers) ─
    int64_t  value      = 0;
    uint64_t super_mask = 0;
    std::array<int, Width> qubits{};

    // ── Default constructor ───────────────────────────────────────────────────
    qint_t() noexcept {
        qubits.fill(-1);
    }

    // ── Implicit int64_t constructor ──────────────────────────────────────────
    // NOLINTNEXTLINE(google-explicit-constructor)
    qint_t(int64_t v) noexcept : value(v), super_mask(0) {
        qubits.fill(-1);
    }

    // ── Construct from qbool (zero-extend bit 0) ──────────────────────────────
    // spec §7: value = b.value ? 1 : 0; mask = b.is_super ? 1 : 0;
    // qubits[0] shares the qbool's qubit index.
    // NOLINTNEXTLINE(google-explicit-constructor)
    qint_t(const qbool& b) noexcept
        : value(b.value ? 1 : 0),
          super_mask(b.is_super ? 1ULL : 0ULL) {
        qubits.fill(-1);
        qubits[0] = b.qubits[0];
    }

    // ── Explicit conversion to qbool (keep bit 0 only) ────────────────────────
    explicit operator qbool() const noexcept {
        qbool out;
        out.value     = (value & 1) != 0;
        out.is_super  = (super_mask & 1) != 0;
        out.qubits[0] = qubits[0];
        return out;
    }

    // ── Explicit int64_t conversion ───────────────────────────────────────────
    // TODO(backend): perform real quantum measurement and collapse state.
    explicit operator int64_t() const noexcept { return value; }

    // ── Copy constructor ──────────────────────────────────────────────────────
    // Copies value and super_mask but does NOT share qubit indices — the copy
    // starts with all qubits == -1.  Callers that need qubits on the copy must
    // invoke ensure_bit_qubit() or otherwise allocate lazily.  This mirrors the
    // qbool copy-constructor semantics and prevents double-release when
    // dispatch helpers take mutable value-copies of operands.
    qint_t(const qint_t& other) noexcept
        : value(other.value), super_mask(other.super_mask) {
        qubits.fill(-1);
    }

    // ── Move constructor ──────────────────────────────────────────────────────
    qint_t(qint_t&& other) noexcept
        : value(other.value), super_mask(other.super_mask), qubits(other.qubits) {
        other.qubits.fill(-1);
        other.super_mask = 0;
    }

    // ── Destructor ────────────────────────────────────────────────────────────
    ~qint_t() {
        for (int idx : qubits) {
            if (idx >= 0) {
                QubitPool::instance().release(idx);
            }
        }
    }

    // ── Copy assignment ───────────────────────────────────────────────────────
    // Releases current qubits, then copies value/super_mask only.
    // Qubit indices are reset to -1 (no shared ownership; prevents double-release).
    qint_t& operator=(const qint_t& other) {
        if (this == &other) return *this;
        for (int idx : qubits) {
            if (idx >= 0) QubitPool::instance().release(idx);
        }
        value      = other.value;
        super_mask = other.super_mask;
        qubits.fill(-1);
        return *this;
    }

    // ── Move assignment ───────────────────────────────────────────────────────
    qint_t& operator=(qint_t&& other) noexcept {
        if (this == &other) return *this;
        for (int idx : qubits) {
            if (idx >= 0) QubitPool::instance().release(idx);
        }
        value      = other.value;
        super_mask = other.super_mask;
        qubits     = other.qubits;
        other.qubits.fill(-1);
        other.super_mask = 0;
        return *this;
    }

    // ── int64_t assignment ────────────────────────────────────────────────────
    qint_t& operator=(int64_t v) {
        for (int idx : qubits) {
            if (idx >= 0) QubitPool::instance().release(idx);
        }
        value      = v;
        super_mask = 0;
        qubits.fill(-1);
        return *this;
    }

    // ── qubits_vec ────────────────────────────────────────────────────────────
    [[nodiscard]] std::vector<int> qubits_vec() const {
        std::vector<int> v;
        v.reserve(Width);
        for (auto idx : qubits) {
            if (idx >= 0) v.push_back(idx);
        }
        return v;
    }

    // ── Arithmetic operator declarations ──────────────────────────────────────
    // Bodies defined as non-member templates in qint_arith.hpp
    qint_t& operator+=(const qint_t& b);
    qint_t& operator-=(const qint_t& b);
    qint_t& operator*=(const qint_t& b);
    qint_t& operator/=(const qint_t& b);
    qint_t& operator%=(const qint_t& b);

    // ── Bitwise operator declarations ─────────────────────────────────────────
    // Bodies defined as non-member templates in qint_bitwise.hpp
    qint_t& operator&=(const qint_t& b);
    qint_t& operator|=(const qint_t& b);
    qint_t& operator^=(const qint_t& b);
    qint_t& operator<<=(int n);
    qint_t& operator>>=(int n);

    // ── Compare operator declarations ─────────────────────────────────────────
    // Returns qbool; bodies in qint_compare.hpp
    qbool operator==(const qint_t& b) const;
    qbool operator!=(const qint_t& b) const;
    qbool operator< (const qint_t& b) const;
    qbool operator<=(const qint_t& b) const;
    qbool operator> (const qint_t& b) const;
    qbool operator>=(const qint_t& b) const;

    // ── Bit subscript (returns qbool view) ───────────────────────────────────
    // Defined in qint_compare.hpp
    qbool operator[](std::size_t i) const;

    // ── PhiProxy ──────────────────────────────────────────────────────────────
    struct PhiProxy {
        qint_t& parent;
        void operator+=(double delta) {
            const int ctrl = detail::current_control
                             ? detail::current_control->qubits[0] : -1;
            for (std::size_t i = 0; i < Width; ++i) {
                if (parent.qubits[i] >= 0) {
                    current_sink()->phi_add(parent.qubits[i], delta, ctrl);
                }
            }
        }
        void operator-=(double delta) { operator+=(-delta); }
    };

    // ── ThetaProxy ────────────────────────────────────────────────────────────
    struct ThetaProxy {
        qint_t& parent;
        void operator+=(double delta) {
            const int ctrl = detail::current_control
                             ? detail::current_control->qubits[0] : -1;
            for (std::size_t i = 0; i < Width; ++i) {
                if (parent.qubits[i] >= 0) {
                    current_sink()->theta_add(parent.qubits[i], delta, ctrl);
                }
            }
        }
        void operator-=(double delta) { operator+=(-delta); }
    };

    PhiProxy   phi()   { return PhiProxy{*this}; }
    ThetaProxy theta() { return ThetaProxy{*this}; }
};

} // namespace sturm
