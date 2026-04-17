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
//
// Phase K PK-3 (sturm-pzye): the uncompute_op tagged union and qint_base
// wrapper were retired — destructor-driven inverse emission no longer
// exists. Inverse gate streams are now emitted by transpiler-synthesised
// `uncompute_*` free-function calls (see include/sturm/uncompute/
// uncompute_api.hpp). The destructor below is a plain pool-release loop
// per principle B10: "Uncomputation is a compile-time concern, not a
// runtime concern. Destructors release qubit indices; they do not emit
// gates."

#include "sturm/qtypes/qint_fwd.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/counter_sink.hpp"  // current_sink()
#include "sturm/control/when_fwd.hpp"   // detail::current_control for proxies

#ifdef STURM_BACKEND_ENABLED
#  include "sturm/core/context.hpp"            // BackendContext, execute_gate (M3)
#  include "sturm/ops/lifted_primitives.hpp"   // emit_RZ_lifted / emit_RY_lifted (sturm-b1g)
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sturm {

// Forward declaration — full definition is in qbool.hpp.
// qint_core.hpp may not include qbool.hpp directly: once M5 lands, qbool.hpp
// will include qint_core.hpp (for inheritance), which would create a cycle.
// Conversion bodies live in qint_qbool_conv.hpp (included by qint.hpp after
// both headers are available).
class qbool;

// Forward declaration — full definition is in bit_proxy.hpp.
// M2: BitProxy is backend-only; declared here so qint_t can declare the
// non-const operator[] overload that returns BitProxy.
#ifdef STURM_BACKEND_ENABLED
struct BitProxy;
#endif

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

    // ── Ownership flag (M1: owning_ on qint_t) ────────────────────────────────
    // When true (default), the destructor releases qubit indices back to the pool.
    // When false, this object is a non-owning view — qubits are managed elsewhere.
    // Transferred by move; copies always start with owning_ = true and no qubits.
    bool owning_ = true;

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
    // Body is in qint_qbool_conv.hpp (needs full qbool definition).
    // NOLINTNEXTLINE(google-explicit-constructor)
    qint_t(const qbool& b) noexcept;

    // ── Explicit conversion to qbool (keep bit 0 only) ────────────────────────
    // Body is in qint_qbool_conv.hpp (needs full qbool definition).
    explicit operator qbool() const noexcept;

    // ── Explicit int64_t conversion ───────────────────────────────────────────
    // TODO(backend): perform real quantum measurement and collapse state.
    explicit operator int64_t() const noexcept { return value; }

    // ── Copy constructor ──────────────────────────────────────────────────────
    // Copies value and super_mask but does NOT share qubit indices — the copy
    // starts with all qubits == -1.  Callers that need qubits on the copy must
    // invoke ensure_bit_qubit() or otherwise allocate lazily.  This mirrors the
    // qbool copy-constructor semantics and prevents double-release when
    // dispatch helpers take mutable value-copies of operands.
    // owning_ = true: the copy owns its own fresh (unallocated) qubits.
    qint_t(const qint_t& other) noexcept
        : value(other.value), super_mask(other.super_mask), owning_(true) {
        qubits.fill(-1);
    }

    // ── Move constructor ──────────────────────────────────────────────────────
    // Transfers ownership: destination becomes owning, source becomes non-owning.
    qint_t(qint_t&& other) noexcept
        : value(other.value), super_mask(other.super_mask), qubits(other.qubits),
          owning_(other.owning_)
    {
        other.owning_ = false;   // source no longer owns the qubits
        other.qubits.fill(-1);
        other.super_mask = 0;
    }

    // ── Destructor ────────────────────────────────────────────────────────────
    // Phase K PK-3 (principle B10): destructors release qubit indices back to
    // the pool. They do not emit gates. Any inverse gate stream required by
    // the forward operation is emitted by a transpiler-synthesised
    // `uncompute_*` free-function call from include/sturm/uncompute/
    // uncompute_api.hpp — placed at the appropriate point in the caller's
    // scope by sturm-transpile, not at destructor time.
    ~qint_t() {
        // Release qubit indices back to the global pool.
        // Guard with owning_ so non-owning views don't double-release.
        // TODO(backend): migrate to per-context pool when the full
        //                qubit-lifecycle wiring lands (M-future).
        if (owning_) {
            for (int idx : qubits) {
                if (idx >= 0) {
                    QubitPool::instance().release(idx);
                }
            }
        }
    }

    // ── Copy assignment ───────────────────────────────────────────────────────
    // Releases current qubits (if owning), then copies value/super_mask only.
    // Qubit indices are reset to -1 (no shared ownership; prevents double-release).
    // owning_ = true: the copy owns its own fresh (unallocated) qubits.
    qint_t& operator=(const qint_t& other) {
        if (this == &other) return *this;
        if (owning_) {
            for (int idx : qubits) {
                if (idx >= 0) QubitPool::instance().release(idx);
            }
        }
        value      = other.value;
        super_mask = other.super_mask;
        qubits.fill(-1);
        owning_    = true;  // copy owns its own fresh qubits
        return *this;
    }

    // ── Move assignment ───────────────────────────────────────────────────────
    // Transfers ownership: destination becomes owning, source becomes non-owning.
    qint_t& operator=(qint_t&& other) noexcept {
        if (this == &other) return *this;
        if (owning_) {
            for (int idx : qubits) {
                if (idx >= 0) QubitPool::instance().release(idx);
            }
        }
        value      = other.value;
        super_mask = other.super_mask;
        qubits     = other.qubits;
        owning_    = other.owning_;  // transfer ownership
        other.owning_ = false;       // source no longer owns the qubits
        other.qubits.fill(-1);
        other.super_mask = 0;
        return *this;
    }

    // ── int64_t assignment ────────────────────────────────────────────────────
    qint_t& operator=(int64_t v) {
        if (owning_) {
            for (int idx : qubits) {
                if (idx >= 0) QubitPool::instance().release(idx);
            }
        }
        value      = v;
        super_mask = 0;
        qubits.fill(-1);
        owning_    = true;
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

    // ── Non-const bit subscript (returns mutable BitProxy) ───────────────────
    // M2: Returns a BitProxy that writes back to this register.
    // Defined in qint_compare.hpp.  Only available when backend is enabled
    // (BitProxy is a backend-only type).
#ifdef STURM_BACKEND_ENABLED
    BitProxy operator[](std::size_t i);
#endif

    // ── PhiProxy ──────────────────────────────────────────────────────────────
    struct PhiProxy {
        qint_t& parent;
        void operator+=(double delta) {
#ifdef STURM_BACKEND_ENABLED
            if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
                // M15: Auto-promote fully-classical registers.
                // If ALL bits are unallocated (fully classical register), allocate
                // qubits, set super_mask, and emit X gate for bits with value 1
                // before emitting the rotation gates.
                {
                    bool all_unallocated = true;
                    for (std::size_t i = 0; i < Width; ++i) {
                        if (parent.qubits[i] >= 0) { all_unallocated = false; break; }
                    }
                    if (all_unallocated) {
                        for (std::size_t i = 0; i < Width; ++i) {
                            parent.qubits[i] = QubitPool::instance().allocate();
                            parent.super_mask |= (1ULL << i);
                            if ((parent.value >> static_cast<int>(i)) & 1) {
                                const auto q = static_cast<uint32_t>(parent.qubits[i]);
                                execute_gate(*ctx, STURM_GATE_X, &q, 1u, 0.0);
                            }
                        }
                    }
                }
                for (std::size_t i = 0; i < Width; ++i) {
                    if (parent.qubits[i] >= 0) {
                        emit_RZ_lifted(*ctx,
                                       static_cast<uint32_t>(parent.qubits[i]),
                                       delta);
                    }
                }
                return;
            }
#endif
            // Fallback: sink path.
            // M15: Auto-promote fully-classical registers.
            // If ALL bits are unallocated (fully classical register), allocate
            // qubits, set super_mask, and call prepare(qubit, 1.0) for bits with
            // classical value 1 before emitting the rotation records.
            {
                bool all_unallocated = true;
                for (std::size_t i = 0; i < Width; ++i) {
                    if (parent.qubits[i] >= 0) { all_unallocated = false; break; }
                }
                if (all_unallocated) {
                    for (std::size_t i = 0; i < Width; ++i) {
                        parent.qubits[i] = QubitPool::instance().allocate();
                        parent.super_mask |= (1ULL << i);
                        if ((parent.value >> static_cast<int>(i)) & 1) {
                            current_sink()->prepare(parent.qubits[i], 1.0);
                        }
                    }
                }
            }
            const int ctrl = detail::current_control
                             ? detail::current_control_qubit : -1;
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
#ifdef STURM_BACKEND_ENABLED
            if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
                // M16: Auto-promote fully-classical registers.
                // If ALL bits are unallocated (fully classical register), allocate
                // qubits, set super_mask, and emit X gate for bits with value 1
                // before emitting the rotation gates.
                {
                    bool all_unallocated = true;
                    for (std::size_t i = 0; i < Width; ++i) {
                        if (parent.qubits[i] >= 0) { all_unallocated = false; break; }
                    }
                    if (all_unallocated) {
                        for (std::size_t i = 0; i < Width; ++i) {
                            parent.qubits[i] = QubitPool::instance().allocate();
                            parent.super_mask |= (1ULL << i);
                            if ((parent.value >> static_cast<int>(i)) & 1) {
                                const auto q = static_cast<uint32_t>(parent.qubits[i]);
                                execute_gate(*ctx, STURM_GATE_X, &q, 1u, 0.0);
                            }
                        }
                    }
                }
                for (std::size_t i = 0; i < Width; ++i) {
                    if (parent.qubits[i] >= 0) {
                        emit_RY_lifted(*ctx,
                                       static_cast<uint32_t>(parent.qubits[i]),
                                       delta);
                    }
                }
                return;
            }
#endif
            // Fallback: sink path.
            // M16: Auto-promote fully-classical registers.
            // If ALL bits are unallocated (fully classical register), allocate
            // qubits, set super_mask, and call prepare(qubit, 1.0) for bits with
            // classical value 1 before emitting the rotation records.
            {
                bool all_unallocated = true;
                for (std::size_t i = 0; i < Width; ++i) {
                    if (parent.qubits[i] >= 0) { all_unallocated = false; break; }
                }
                if (all_unallocated) {
                    for (std::size_t i = 0; i < Width; ++i) {
                        parent.qubits[i] = QubitPool::instance().allocate();
                        parent.super_mask |= (1ULL << i);
                        if ((parent.value >> static_cast<int>(i)) & 1) {
                            current_sink()->prepare(parent.qubits[i], 1.0);
                        }
                    }
                }
            }
            const int ctrl = detail::current_control
                             ? detail::current_control_qubit : -1;
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

    // ── make_non_owning ───────────────────────────────────────────────────────
    // Creates a non-owning view of externally managed qubits.
    // The returned object's destructor will NOT release the qubit indices.
    // Used by dispatch helpers and qbool's make_non_owning to avoid double-release.
    static qint_t make_non_owning(std::array<int, Width> q, int64_t val,
                                  uint64_t mask) {
        qint_t result;
        result.qubits     = q;
        result.value      = val;
        result.super_mask = mask;
        result.owning_    = false;
        return result;
    }
};

} // namespace sturm
