/* gate_kind.h — M1: STURM virtual gate set, taxonomy table.
 *
 * C header (usable from both C and C++).
 * Defines the 18 primitive gates in PRD §4 order,
 * the classical_effect taxonomy, and the lookup function.
 */

#ifndef STURM_CORE_GATE_KIND_H
#define STURM_CORE_GATE_KIND_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Gate kind enum (18 entries, 0-based, PRD §4 order) ────────────────────── */

typedef enum sturm_gate_kind {
    STURM_GATE_X    =  0,  /* arity 1, FLIP,   permutation */
    STURM_GATE_Y    =  1,  /* arity 1, FLIP,   permutation */
    STURM_GATE_Z    =  2,  /* arity 1, NONE,   no permutation */
    STURM_GATE_H    =  3,  /* arity 1, BRANCH, no permutation */
    STURM_GATE_S    =  4,  /* arity 1, NONE,   no permutation */
    STURM_GATE_T    =  5,  /* arity 1, NONE,   no permutation */
    STURM_GATE_P    =  6,  /* arity 1, NONE,   no permutation  (param = theta) */
    STURM_GATE_RX   =  7,  /* arity 1, BRANCH, no permutation  (param = theta) */
    STURM_GATE_RY   =  8,  /* arity 1, BRANCH, no permutation  (param = theta) */
    STURM_GATE_RZ   =  9,  /* arity 1, NONE,   no permutation  (param = theta) */
    STURM_GATE_CX   = 10,  /* arity 2, FLIP,   permutation */
    STURM_GATE_CY   = 11,  /* arity 2, FLIP,   permutation */
    STURM_GATE_CZ   = 12,  /* arity 2, NONE,   no permutation */
    STURM_GATE_CRX  = 13,  /* arity 2, BRANCH, no permutation  (param = theta) */
    STURM_GATE_CRY  = 14,  /* arity 2, BRANCH, no permutation  (param = theta) */
    STURM_GATE_CRZ  = 15,  /* arity 2, NONE,   no permutation  (param = theta) */
    STURM_GATE_CCX  = 16,  /* arity 3, FLIP,   permutation */
    STURM_GATE_SWAP = 17,  /* arity 2, FLIP,   permutation */

    STURM_GATE_COUNT = 18  /* sentinel — number of gates */
} sturm_gate_kind_t;

/* ── Classical effect enum ──────────────────────────────────────────────────── */

typedef enum sturm_classical_effect {
    STURM_CE_NONE   = 0,  /* phase-only; all-classical operands → skip */
    STURM_CE_FLIP   = 1,  /* permutation; all-classical → mutate values */
    STURM_CE_BRANCH = 2   /* creates superposition; classical → promote */
} sturm_classical_effect_t;

/* ── Gate info struct ───────────────────────────────────────────────────────── */

typedef struct sturm_gate_info {
    uint8_t                  arity;       /* number of qubit operands */
    sturm_classical_effect_t effect;      /* classical-operand dispatch rule */
    bool                     permutation; /* true iff gate permutes comp. basis */
    const char*              name;        /* human-readable name, NUL-terminated */
} sturm_gate_info_t;

/* ── Lookup function ────────────────────────────────────────────────────────── */

/* Returns a pointer to the static gate info for the given kind.
 * The pointer is stable for the lifetime of the program.
 * Returns NULL if kind >= STURM_GATE_COUNT (should never happen in correct code).
 */
const sturm_gate_info_t* sturm_gate_info_of(sturm_gate_kind_t kind);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* STURM_CORE_GATE_KIND_H */
