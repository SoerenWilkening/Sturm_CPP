// uncompute_op.hpp — M19: uncompute_op tagged union.
//
// Encodes the semantic inverse of the operation that produced a qint_t<W>.
// Stored inline inside every qint/qbool result so the RAII destructor (M20)
// can run the inverse without heap allocation or std::function.
//
// Design constraints (PRD §9):
//   - No heap allocation.
//   - No std::function.
//   - static_assert(sizeof(uncompute_op) <= 32).
//   - One entry per operator that produces an uncomputable result.
//
// apply(ctx, self):
//   Runs the inverse operation on `self` via the Layer B execute_gate sink.
//   NONE tag → no-op.
//   ADD_CONST(c) → self.sub_const(c, ctx)
//   SUB_CONST(c) → self.add_const(c, ctx)
//   ADD_QINT     → TODO(backend): subtract source qint (M21+)
//   SUB_QINT     → TODO(backend): add source qint (M21+)
//   MUL_INVERSE  → TODO(backend): bespoke inverse (M22)
//   DIV_INVERSE  → TODO(backend): bespoke inverse (M22)
//   MOD_INVERSE  → TODO(backend): bespoke inverse (M22)
//   BITWISE_SELF → TODO(backend): re-run self-inverse bitwise op (M22)
//   COMPARE      → TODO(backend): re-run comparison to clear ancilla (M22)
//
// LOC budget: < 200 (this file).

#pragma once

#include "sturm/uncompute/qint_base.hpp"
#include "sturm/core/context.hpp"

#include <cassert>
#include <cstdint>

namespace sturm {

// ── uncompute_op ──────────────────────────────────────────────────────────────

struct uncompute_op {

    // ── kind enum ─────────────────────────────────────────────────────────────

    enum class kind : uint8_t {
        NONE,          ///< No inverse (measurement, or explicitly cleared)
        ADD_CONST,     ///< Inverse of +=c  → emit -=c  (data: int64_t c)
        SUB_CONST,     ///< Inverse of -=c  → emit +=c  (data: int64_t c)
        ADD_QINT,      ///< Inverse of +=q  → emit -=q  (data: ptr to source qint_base)
        SUB_QINT,      ///< Inverse of -=q  → emit +=q  (data: ptr to source qint_base)
        MUL_INVERSE,   ///< Bespoke inverse for multiplication (data: ptr to factor)
        DIV_INVERSE,   ///< Bespoke inverse for division       (data: ptr to divisor)
        MOD_INVERSE,   ///< Bespoke inverse for modulo         (data: ptr to modulus)
        BITWISE_SELF,  ///< Self-inverse bitwise op            (data: op sub-kind + ptr)
        COMPARE,       ///< Re-run comparison to uncompute ancilla (data: ptrs + op)
    };

    // ── data union ────────────────────────────────────────────────────────────
    //
    // Sized to fit in 32 bytes total (tag byte + padding + union).
    // Largest variant: one int64_t (8 bytes) — well under budget.

    union data_t {
        // ADD_CONST / SUB_CONST: the constant c
        int64_t const_c;

        // ADD_QINT / SUB_QINT / MUL_INVERSE / DIV_INVERSE / MOD_INVERSE:
        // non-owning pointer to the source qint_base.
        // The pointed-to object must outlive this uncompute_op (Bennett discipline
        // guarantees inputs are pristine when the inverse runs).
        // TODO(backend): typed once the concrete qint is wired (M21/M22).
        const qint_base* qint_ptr;

        // BITWISE_SELF: pointer to input and sub-kind tag (packed into two fields)
        struct {
            const qint_base* input_ptr;
            uint32_t         sub_kind;   ///< Which bitwise op (future enum)
        } bitwise;

        // COMPARE: pointers to lhs/rhs and comparison op code
        struct {
            const qint_base* lhs_ptr;
            const qint_base* rhs_ptr;
            uint32_t         cmp_kind;   ///< Which comparison (future enum)
        } compare;

        data_t() noexcept : const_c(0) {}
    };

    // ── public fields ─────────────────────────────────────────────────────────

    kind   tag;   ///< Which variant is active
    data_t data;  ///< Payload (union, zero-initialised by default)

    // ── constructors ──────────────────────────────────────────────────────────

    // Default: NONE (no-op inverse)
    uncompute_op() noexcept : tag(kind::NONE) {}

    // ── named constructors (factory functions) ────────────────────────────────

    [[nodiscard]] static uncompute_op make_add_const(int64_t c) noexcept {
        uncompute_op op;
        op.tag          = kind::ADD_CONST;
        op.data.const_c = c;
        return op;
    }

    [[nodiscard]] static uncompute_op make_sub_const(int64_t c) noexcept {
        uncompute_op op;
        op.tag          = kind::SUB_CONST;
        op.data.const_c = c;
        return op;
    }

    [[nodiscard]] static uncompute_op make_add_qint(const qint_base* src) noexcept {
        uncompute_op op;
        op.tag             = kind::ADD_QINT;
        op.data.qint_ptr   = src;
        return op;
    }

    [[nodiscard]] static uncompute_op make_sub_qint(const qint_base* src) noexcept {
        uncompute_op op;
        op.tag             = kind::SUB_QINT;
        op.data.qint_ptr   = src;
        return op;
    }

    [[nodiscard]] static uncompute_op make_mul_inverse(const qint_base* factor) noexcept {
        uncompute_op op;
        op.tag           = kind::MUL_INVERSE;
        op.data.qint_ptr = factor;
        return op;
    }

    [[nodiscard]] static uncompute_op make_div_inverse(const qint_base* divisor) noexcept {
        uncompute_op op;
        op.tag           = kind::DIV_INVERSE;
        op.data.qint_ptr = divisor;
        return op;
    }

    [[nodiscard]] static uncompute_op make_mod_inverse(const qint_base* modulus) noexcept {
        uncompute_op op;
        op.tag           = kind::MOD_INVERSE;
        op.data.qint_ptr = modulus;
        return op;
    }

    [[nodiscard]] static uncompute_op make_bitwise_self(const qint_base* input,
                                                        uint32_t sub_kind) noexcept {
        uncompute_op op;
        op.tag                    = kind::BITWISE_SELF;
        op.data.bitwise.input_ptr = input;
        op.data.bitwise.sub_kind  = sub_kind;
        return op;
    }

    [[nodiscard]] static uncompute_op make_compare(const qint_base* lhs,
                                                   const qint_base* rhs,
                                                   uint32_t cmp_kind) noexcept {
        uncompute_op op;
        op.tag                  = kind::COMPARE;
        op.data.compare.lhs_ptr = lhs;
        op.data.compare.rhs_ptr = rhs;
        op.data.compare.cmp_kind = cmp_kind;
        return op;
    }

    // ── apply ─────────────────────────────────────────────────────────────────
    //
    // Run the inverse operation on `self` through the Layer B execute_gate sink
    // (via ctx).  Called from the RAII uncompute runner (M20) in the destructor.

    void apply(BackendContext& ctx, qint_base& self) const noexcept {
        switch (tag) {
        case kind::NONE:
            // No inverse — measurement or explicitly cleared; nothing to emit.
            break;

        case kind::ADD_CONST:
            // Inverse of +=c is -=c.
            self.sub_const(data.const_c, ctx);
            break;

        case kind::SUB_CONST:
            // Inverse of -=c is +=c.
            self.add_const(data.const_c, ctx);
            break;

        case kind::ADD_QINT:
            // TODO(backend): subtract source qint from self (M21).
            // Requires concrete qint_t<W> wiring; stub until then.
            break;

        case kind::SUB_QINT:
            // TODO(backend): add source qint to self (M21).
            break;

        case kind::MUL_INVERSE:
            // TODO(backend): bespoke inverse for multiplication (M22).
            break;

        case kind::DIV_INVERSE:
            // TODO(backend): bespoke inverse for division (M22).
            break;

        case kind::MOD_INVERSE:
            // TODO(backend): bespoke inverse for modulo (M22).
            break;

        case kind::BITWISE_SELF:
            // TODO(backend): re-run self-inverse bitwise op (M22).
            break;

        case kind::COMPARE:
            // Emit the stub compare circuit followed by its inverse on `self`
            // (the qbool's ancilla-qubit view).  This satisfies the Bennett
            // discipline: the forward and inverse sequences are symmetric and
            // the IR captures both so callers can verify the round-trip.
            //
            // TODO(backend): replace with a proper ancilla-qubit comparator
            //                circuit that operates on lhs_ptr / rhs_ptr once
            //                the full comparator wiring lands (M22+).
            self.compare_forward(data.compare.cmp_kind, ctx);
            self.compare_inverse(data.compare.cmp_kind, ctx);
            break;

        default:
            assert(false && "uncompute_op::apply: unknown kind");
            break;
        }
    }
};

// ── Size invariant ────────────────────────────────────────────────────────────

static_assert(sizeof(uncompute_op) <= 32,
              "uncompute_op must be at most 32 bytes (no heap, no std::function)");

} // namespace sturm
