/* gate_kind.c — M1: Static taxonomy table for STURM virtual gate set.
 *
 * One row per gate in PRD §4 order.
 * This file has no dependencies beyond gate_kind.h and the C standard library.
 */

#include "sturm/core/gate_kind.h"

/* ── Static table ───────────────────────────────────────────────────────────── */

static const sturm_gate_info_t kGateTable[STURM_GATE_COUNT] = {
    /* STURM_GATE_X    */ { 1, STURM_CE_FLIP,   true,  "X"    },
    /* STURM_GATE_Y    */ { 1, STURM_CE_FLIP,   true,  "Y"    },
    /* STURM_GATE_Z    */ { 1, STURM_CE_NONE,   false, "Z"    },
    /* STURM_GATE_H    */ { 1, STURM_CE_BRANCH, false, "H"    },
    /* STURM_GATE_S    */ { 1, STURM_CE_NONE,   false, "S"    },
    /* STURM_GATE_T    */ { 1, STURM_CE_NONE,   false, "T"    },
    /* STURM_GATE_P    */ { 1, STURM_CE_NONE,   false, "P"    },
    /* STURM_GATE_RX   */ { 1, STURM_CE_BRANCH, false, "Rx"   },
    /* STURM_GATE_RY   */ { 1, STURM_CE_BRANCH, false, "Ry"   },
    /* STURM_GATE_RZ   */ { 1, STURM_CE_NONE,   false, "Rz"   },
    /* STURM_GATE_CX   */ { 2, STURM_CE_FLIP,   true,  "CX"   },
    /* STURM_GATE_CY   */ { 2, STURM_CE_FLIP,   true,  "CY"   },
    /* STURM_GATE_CZ   */ { 2, STURM_CE_NONE,   false, "CZ"   },
    /* STURM_GATE_CRX  */ { 2, STURM_CE_BRANCH, false, "CRx"  },
    /* STURM_GATE_CRY  */ { 2, STURM_CE_BRANCH, false, "CRy"  },
    /* STURM_GATE_CRZ  */ { 2, STURM_CE_NONE,   false, "CRz"  },
    /* STURM_GATE_CCX  */ { 3, STURM_CE_FLIP,   true,  "CCX"  },
    /* STURM_GATE_SWAP */ { 2, STURM_CE_FLIP,   true,  "SWAP" },
};

/* ── Lookup function ────────────────────────────────────────────────────────── */

const sturm_gate_info_t* sturm_gate_info_of(sturm_gate_kind_t kind) {
    if ((unsigned)kind >= (unsigned)STURM_GATE_COUNT) {
        return (const sturm_gate_info_t*)0; /* NULL */
    }
    return &kGateTable[(int)kind];
}
