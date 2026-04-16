// fresh_names.hpp — header-only FreshNameAllocator for Phase E temporaries
// and Phase G nested-WHEN control temporaries.
//
// Phase E of the STURM transpiler (docs/implementation_plan_transpiler_phase_e.md)
// decomposes compound qbool expressions like `qbool r = (b | c) & d;` into a
// flat sequence of single-operator VarDecls with named intermediates:
//
//     qbool __stu_t0 = b | c;
//     qbool r = __stu_t0 & d;
//
// Each intermediate needs a unique name. FreshNameAllocator hands those names
// out in order — `__stu_t0`, `__stu_t1`, `__stu_t2`, ... — using the exact
// spelling the roadmap specifies (no underscore between `stu` and `t`).
//
// Phase G (docs/implementation_plan_transpiler_phase_g.md) lowers nested
// `WHEN(outer) { WHEN(inner) { ... } }` into an explicit AND-temp + an
// `uncompute_and` call. The AND-temp uses a *separate* name family,
// `__stu_ctrl0`, `__stu_ctrl1`, ..., handed out by `next_ctrl()`. A distinct
// counter is important for two reasons:
//   1. The generated control names must not collide with Phase E/F's
//      `__stu_t<N>` temps that may appear in the same translation unit.
//   2. If Phase F (single-op lifts) and Phase G (nested-WHEN lifts) fire in
//      the same TU, each phase's sequence is monotonic in its own namespace
//      without one phase's allocations renumbering the other. A Phase F lift
//      inserted between two nested-WHEN pairs does not push later
//      `__stu_ctrl<M>` indices around.
//
// Scope
// -----
// The allocator is **per-QUnit** (i.e. per translation unit). Every STURM
// source file gets a brand-new allocator, and both counters reset to zero.
// This is the simplest rule that still guarantees global uniqueness within a
// TU, because every temporary name is unique inside its own file and the
// names never leak across TUs (they are function-scope locals in the emitted
// output).
//
// Thread safety
// -------------
// None. The transpiler is single-threaded by construction — LibTooling drives
// one AST at a time — so a plain non-atomic counter is fine.
//
// Why a header-only class instead of a free function?
// ---------------------------------------------------
// The counter must be owned by *something*, and tying it to the QUnit is the
// cleanest place because the QUnit is already the boundary of a transpile.
// A header-only class keeps the linkage surface zero and lets every matcher
// module include this file without pulling in a new TU.

#ifndef STURM_TRANSPILE_FRESH_NAMES_HPP
#define STURM_TRANSPILE_FRESH_NAMES_HPP

#include <cstddef>
#include <string>

namespace sturm::transpile {

// Hands out monotonically-increasing temporary names. Two independent
// counters are owned by each instance:
//
//   - next()       → "__stu_t0", "__stu_t1", ...         (Phase E/F temps)
//   - next_ctrl()  → "__stu_ctrl0", "__stu_ctrl1", ...   (Phase G AND-temps)
//
// The counters share no state. Calling next() never advances the ctrl
// counter and vice versa — a guarantee Phase G depends on so its
// `__stu_ctrl<M>` indices stay stable when Phase F temps are interleaved.
// Different allocator instances always have independent counters.
class FreshNameAllocator {
public:
    FreshNameAllocator() = default;

    // Allocate and return the next Phase E/F temp name. The first call
    // returns "__stu_t0", the second "__stu_t1", and so on. The returned
    // std::string is owned by the caller — the allocator does not keep a
    // reference to it.
    std::string next() {
        // Build the name by appending the decimal representation of the
        // counter to the fixed prefix. std::to_string is fine here: the
        // counter grows once per compound-expression temporary, so the cost
        // is irrelevant relative to the LibTooling traversal.
        std::string name = "__stu_t";
        name += std::to_string(counter_);
        ++counter_;
        return name;
    }

    // Allocate and return the next Phase G control-temp name. The first call
    // returns "__stu_ctrl0", the second "__stu_ctrl1", and so on. The ctrl
    // counter is fully independent of `next()`'s counter — interleaving the
    // two methods produces `__stu_t<N>` / `__stu_ctrl<M>` sequences with no
    // cross-contamination. Same ownership rule as `next()`: the caller owns
    // the returned string.
    std::string next_ctrl() {
        // Mirrors the `next()` implementation deliberately: the two methods
        // are symmetric and exist only because Phase G wants a distinct name
        // family (`__stu_ctrl<M>`) from Phase E/F's `__stu_t<N>`. Any future
        // change to either formatting rule should be audited on both.
        std::string name = "__stu_ctrl";
        name += std::to_string(ctrl_counter_);
        ++ctrl_counter_;
        return name;
    }

private:
    // The roadmap talks about a "monotonic counter starting at 0"; a plain
    // std::size_t matches that without imposing any artificial cap short of
    // the platform's address space.
    std::size_t counter_ = 0;

    // Phase G's control-temp counter — separate from `counter_` so the two
    // name families never influence each other's numbering.
    std::size_t ctrl_counter_ = 0;
};

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_FRESH_NAMES_HPP
