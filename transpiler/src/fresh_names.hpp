// fresh_names.hpp — header-only FreshNameAllocator for Phase E temporaries.
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
// Scope
// -----
// The allocator is **per-QUnit** (i.e. per translation unit). Every STURM
// source file gets a brand-new allocator, and the counter resets to zero.
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

// Hands out monotonically-increasing temporary names of the form
// `__stu_t<N>` where <N> is the zero-based allocation index. Each call to
// next() returns a fresh name and increments the internal counter; different
// instances have independent counters.
class FreshNameAllocator {
public:
    FreshNameAllocator() = default;

    // Allocate and return the next name. The first call returns "__stu_t0",
    // the second "__stu_t1", and so on. The returned std::string is owned by
    // the caller — the allocator does not keep a reference to it.
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

private:
    // The roadmap talks about a "monotonic counter starting at 0"; a plain
    // std::size_t matches that without imposing any artificial cap short of
    // the platform's address space.
    std::size_t counter_ = 0;
};

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_FRESH_NAMES_HPP
