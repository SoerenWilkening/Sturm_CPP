// uncompute_run.cpp — M20: RAII uncompute runner (translation unit).
//
// The runner is fully inline in uncompute_run.hpp.  This file exists to
// satisfy the module layout required by the implementation plan and to
// provide a concrete translation unit that can be linked when the inline
// implementation is split into out-of-line helpers in future milestones.
//
// TODO(backend): move non-trivial helpers (e.g. a debug-mode qubit-state
// verifier before pool.release) out of the header and into this file once
// the Orkan bridge is wired in M18/M21.

#include "sturm/uncompute/uncompute_run.hpp"

// No out-of-line definitions needed at this milestone.
