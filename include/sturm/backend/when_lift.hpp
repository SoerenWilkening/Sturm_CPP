// when_lift.hpp — M2 (PRD v2): generic control-count lifting over classical primitives.
//
// WhenLift: RAII context that holds a stack of active control qubits.
// Lifted primitive calls (lift_X, lift_XOR, lift_AND, lift_phase, lift_phi_add)
// consult the stack and emit the appropriately controlled gate sequence.
//
// Lifting rules (PRD v2 §4):
//   0 ctrls: direct primitive
//   1 ctrl:  CX / CCX / CRy / CRz
//   2+ ctrls: AND-fold via ancilla (requires AncillaManager)
//
// CRy(θ): CX(ctrl,tgt); Ry(-θ/2)(tgt); CX(ctrl,tgt); Ry(+θ/2)(tgt)
// CRz(θ): CX(ctrl,tgt); Rz(-θ/2)(tgt); CX(ctrl,tgt); Rz(+θ/2)(tgt)
//
// Target: <250 LoC (impl plan M2).

#pragma once

#include "sturm/backend/state.hpp"
#include "sturm/backend/primitives.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/when_lift_dispatch.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace sturm {
namespace v2 {

// ── Controlled phase helpers (CRy / CRz decomposition) ───────────────────────

inline void apply_cry(SimState& s, uint32_t ctrl, uint32_t tgt, double theta) {
    primitive_XOR(s, ctrl, tgt);
    primitive_phase(s, tgt, -theta / 2.0);
    primitive_XOR(s, ctrl, tgt);
    primitive_phase(s, tgt,  theta / 2.0);
}

inline void apply_crz(SimState& s, uint32_t ctrl, uint32_t tgt, double theta) {
    primitive_XOR(s, ctrl, tgt);
    primitive_phi_add(s, tgt, -theta / 2.0);
    primitive_XOR(s, ctrl, tgt);
    primitive_phi_add(s, tgt,  theta / 2.0);
}

// ── WhenLift ──────────────────────────────────────────────────────────────────
//
// RAII scope that holds a stack of active control qubits.
// AncillaManager is optional; pass nullptr when no ancillas are needed
// (0 or 1 control). For 2+ controls an AncillaManager is required.

class WhenLift {
public:
    explicit WhenLift(SimState& s, AncillaManager* mgr = nullptr)
        : s_(s), mgr_(mgr) {}

    WhenLift(const WhenLift&)            = delete;
    WhenLift& operator=(const WhenLift&) = delete;
    ~WhenLift() = default;

    // ── Control management ────────────────────────────────────────────────────

    void push_control(uint32_t qubit) { controls_.push_back(qubit); }

    void pop_control() {
        if (controls_.empty()) {
            throw std::runtime_error("WhenLift::pop_control: control stack is empty");
        }
        controls_.pop_back();
    }

    uint32_t control_depth() const { return static_cast<uint32_t>(controls_.size()); }

    // ── Lifted primitives ─────────────────────────────────────────────────────

    // lift_X: 0→X, 1→CX, 2→CCX, 3+→AND-fold+recurse
    void lift_X(uint32_t tgt) {
        const auto n = controls_.size();
        if (n == 0u) {
            primitive_X(s_, tgt);
        } else if (n == 1u) {
            primitive_XOR(s_, controls_[0], tgt);
        } else if (n == 2u) {
            primitive_AND(s_, controls_[0], controls_[1], tgt);
        } else {
            // AND-fold first two controls into ancilla, recurse, uncompute.
            require_mgr("lift_X with 3+ controls");
            uint32_t anc = mgr_->allocate_ancilla();
            primitive_AND(s_, controls_[0], controls_[1], anc);
            uint32_t c0 = controls_[0];
            uint32_t c1 = controls_[1];
            controls_.erase(controls_.begin(), controls_.begin() + 2);
            controls_.insert(controls_.begin(), anc);
            lift_X(tgt);
            controls_.erase(controls_.begin());
            controls_.insert(controls_.begin(), c0);
            controls_.insert(controls_.begin() + 1, c1);
            primitive_AND(s_, controls_[0], controls_[1], anc);
            mgr_->free_ancilla(anc);
        }
    }

    // lift_XOR: 0→XOR, 1→AND (Toffoli), 2+→c_and_impl
    void lift_XOR(uint32_t src, uint32_t tgt) {
        const auto n = controls_.size();
        if (n == 0u) {
            primitive_XOR(s_, src, tgt);
        } else if (n == 1u) {
            primitive_AND(s_, controls_[0], src, tgt);
        } else {
            require_mgr("lift_XOR with 2+ controls");
            std::vector<uint32_t> all_ctrls = controls_;
            all_ctrls.push_back(src);
            c_and_impl(s_, *mgr_, all_ctrls.data(),
                       static_cast<uint32_t>(all_ctrls.size()), tgt);
        }
    }

    // lift_AND: 0→AND, 1+→c_and_impl
    void lift_AND(uint32_t c0, uint32_t c1, uint32_t tgt) {
        if (controls_.empty()) {
            primitive_AND(s_, c0, c1, tgt);
        } else {
            require_mgr("lift_AND with controls");
            std::vector<uint32_t> all_ctrls = controls_;
            all_ctrls.push_back(c0);
            all_ctrls.push_back(c1);
            c_and_impl(s_, *mgr_, all_ctrls.data(),
                       static_cast<uint32_t>(all_ctrls.size()), tgt);
        }
    }

    // lift_phase (Ry): 0→Ry, 1→CRy, 2+→fold+CRy+unfold
    void lift_phase(uint32_t tgt, double theta) {
        const auto n = controls_.size();
        if (n == 0u) {
            primitive_phase(s_, tgt, theta);
        } else if (n == 1u) {
            apply_cry(s_, controls_[0], tgt, theta);
        } else {
            require_mgr("lift_phase with 2+ controls");
            uint32_t ctrl_anc = fold_controls();
            apply_cry(s_, ctrl_anc, tgt, theta);
            unfold_controls(ctrl_anc);
        }
    }

    // lift_phi_add (Rz): 0→Rz, 1→CRz, 2+→fold+CRz+unfold
    void lift_phi_add(uint32_t tgt, double theta) {
        const auto n = controls_.size();
        if (n == 0u) {
            primitive_phi_add(s_, tgt, theta);
        } else if (n == 1u) {
            apply_crz(s_, controls_[0], tgt, theta);
        } else {
            require_mgr("lift_phi_add with 2+ controls");
            uint32_t ctrl_anc = fold_controls();
            apply_crz(s_, ctrl_anc, tgt, theta);
            unfold_controls(ctrl_anc);
        }
    }

private:
    void require_mgr(const char* op) const {
        if (mgr_ == nullptr) {
            throw std::runtime_error(
                std::string("WhenLift: AncillaManager required for ") + op);
        }
    }

    // Fold all controls into a single ancilla via chained AND.
    // Saves intermediate ancillas in fold_ancillas_ for unfold_controls().
    // Precondition: controls_.size() >= 2, mgr_ != nullptr.
    uint32_t fold_controls() {
        uint32_t cur = controls_[0];
        for (uint32_t i = 1u; i < controls_.size(); ++i) {
            uint32_t anc = mgr_->allocate_ancilla();
            primitive_AND(s_, cur, controls_[i], anc);
            fold_ancillas_.push_back(anc);
            cur = anc;
        }
        return cur;
    }

    // Uncompute fold_controls() in reverse order.
    void unfold_controls(uint32_t /*anc*/) {
        for (int i = static_cast<int>(fold_ancillas_.size()) - 1; i >= 0; --i) {
            uint32_t anc_q = fold_ancillas_[static_cast<uint32_t>(i)];
            uint32_t in0 = (i == 0) ? controls_[0]
                                    : fold_ancillas_[static_cast<uint32_t>(i - 1)];
            uint32_t in1 = controls_[static_cast<uint32_t>(i + 1)];
            primitive_AND(s_, in0, in1, anc_q);
            mgr_->free_ancilla(anc_q);
        }
        fold_ancillas_.clear();
    }

    SimState&              s_;
    AncillaManager*        mgr_;
    std::vector<uint32_t>  controls_;
    std::vector<uint32_t>  fold_ancillas_;
};

} // namespace v2
} // namespace sturm
