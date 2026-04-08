// test_gate_kind.cpp — M1: Gate kind enum + taxonomy table tests.
// TDD: written before implementation, drives the gate_kind.h / gate_kind.c module.

#include "sturm/core/gate_kind.h"

#include <cassert>
#include <cstring>
#include <cstdint>

// ── Expected taxonomy (PRD §4 order) ─────────────────────────────────────────

struct Expected {
    sturm_gate_kind_t kind;
    uint8_t           arity;
    sturm_classical_effect_t effect;
    bool              permutation;
    const char*       name;
};

static const Expected kExpected[] = {
    { STURM_GATE_X,   1, STURM_CE_FLIP,   true,  "X"   },
    { STURM_GATE_Y,   1, STURM_CE_FLIP,   true,  "Y"   },
    { STURM_GATE_Z,   1, STURM_CE_NONE,   false, "Z"   },
    { STURM_GATE_H,   1, STURM_CE_BRANCH, false, "H"   },
    { STURM_GATE_S,   1, STURM_CE_NONE,   false, "S"   },
    { STURM_GATE_T,   1, STURM_CE_NONE,   false, "T"   },
    { STURM_GATE_P,   1, STURM_CE_NONE,   false, "P"   },
    { STURM_GATE_RX,  1, STURM_CE_BRANCH, false, "Rx"  },
    { STURM_GATE_RY,  1, STURM_CE_BRANCH, false, "Ry"  },
    { STURM_GATE_RZ,  1, STURM_CE_NONE,   false, "Rz"  },
    { STURM_GATE_CX,  2, STURM_CE_FLIP,   true,  "CX"  },
    { STURM_GATE_CY,  2, STURM_CE_FLIP,   true,  "CY"  },
    { STURM_GATE_CZ,  2, STURM_CE_NONE,   false, "CZ"  },
    { STURM_GATE_CRX, 2, STURM_CE_BRANCH, false, "CRx" },
    { STURM_GATE_CRY, 2, STURM_CE_BRANCH, false, "CRy" },
    { STURM_GATE_CRZ, 2, STURM_CE_NONE,   false, "CRz" },
    { STURM_GATE_CCX, 3, STURM_CE_FLIP,   true,  "CCX" },
    { STURM_GATE_SWAP,2, STURM_CE_FLIP,   true,  "SWAP"},
};

static constexpr int kNumGates = 18;

// ── Test: exactly 18 entries via STURM_GATE_COUNT ────────────────────────────

static void test_gate_count() {
    assert(STURM_GATE_COUNT == kNumGates);
}

// ── Test: arities match PRD §4 ────────────────────────────────────────────────

static void test_arities() {
    for (int i = 0; i < kNumGates; ++i) {
        const Expected& e = kExpected[i];
        const sturm_gate_info_t* info = sturm_gate_info_of(e.kind);
        assert(info != NULL);
        assert(info->arity == e.arity);
    }
}

// ── Test: FLIP ⇒ permutation is true ─────────────────────────────────────────

static void test_flip_implies_permutation() {
    for (int i = 0; i < kNumGates; ++i) {
        const Expected& e = kExpected[i];
        const sturm_gate_info_t* info = sturm_gate_info_of(e.kind);
        assert(info != NULL);
        if (info->effect == STURM_CE_FLIP) {
            assert(info->permutation &&
                   "FLIP gate must have permutation == true");
        }
    }
}

// ── Test: taxonomy matches literal expected array ─────────────────────────────

static void test_taxonomy_lookup() {
    for (int i = 0; i < kNumGates; ++i) {
        const Expected& e = kExpected[i];
        const sturm_gate_info_t* info = sturm_gate_info_of(e.kind);
        assert(info != NULL);
        assert(info->arity       == e.arity);
        assert(info->effect      == e.effect);
        assert(info->permutation == e.permutation);
        assert(info->name        != NULL);
        assert(std::strcmp(info->name, e.name) == 0);
    }
}

// ── Test: NONE ⇒ permutation is false (for Z, CZ, Rz, CRz, S, T, P, CRx, CRy)
// Note: BRANCH gates that are not permutations are also checked here.

static void test_none_not_permutation() {
    // NONE-effect gates must never have permutation == true
    for (int i = 0; i < kNumGates; ++i) {
        const Expected& e = kExpected[i];
        const sturm_gate_info_t* info = sturm_gate_info_of(e.kind);
        assert(info != NULL);
        if (info->effect == STURM_CE_NONE) {
            assert(!info->permutation &&
                   "NONE-effect gate must have permutation == false");
        }
    }
}

// ── Test: sturm_gate_info_of returns distinct pointers per entry, or same
//    stable pointer — just not NULL and consistent when called twice.

static void test_stable_pointer() {
    for (int i = 0; i < kNumGates; ++i) {
        const sturm_gate_info_t* p1 = sturm_gate_info_of((sturm_gate_kind_t)i);
        const sturm_gate_info_t* p2 = sturm_gate_info_of((sturm_gate_kind_t)i);
        assert(p1 != NULL);
        assert(p1 == p2 && "sturm_gate_info_of must return a stable pointer");
    }
}

// ── Test: gate kind enum values are 0-based and contiguous ───────────────────

static void test_enum_ordering() {
    // Verify each expected entry has the enum value equal to its index
    assert((int)STURM_GATE_X    == 0);
    assert((int)STURM_GATE_Y    == 1);
    assert((int)STURM_GATE_Z    == 2);
    assert((int)STURM_GATE_H    == 3);
    assert((int)STURM_GATE_S    == 4);
    assert((int)STURM_GATE_T    == 5);
    assert((int)STURM_GATE_P    == 6);
    assert((int)STURM_GATE_RX   == 7);
    assert((int)STURM_GATE_RY   == 8);
    assert((int)STURM_GATE_RZ   == 9);
    assert((int)STURM_GATE_CX   == 10);
    assert((int)STURM_GATE_CY   == 11);
    assert((int)STURM_GATE_CZ   == 12);
    assert((int)STURM_GATE_CRX  == 13);
    assert((int)STURM_GATE_CRY  == 14);
    assert((int)STURM_GATE_CRZ  == 15);
    assert((int)STURM_GATE_CCX  == 16);
    assert((int)STURM_GATE_SWAP == 17);
    assert((int)STURM_GATE_COUNT == 18);
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    test_gate_count();
    test_enum_ordering();
    test_arities();
    test_flip_implies_permutation();
    test_none_not_permutation();
    test_taxonomy_lookup();
    test_stable_pointer();
    return 0;
}
