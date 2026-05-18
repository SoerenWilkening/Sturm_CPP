// sturm-0tcv: entry_point attribute snapshot — bool-returning
// invariant check.
//
// Exercises the matcher_main_lifecycle (sturm-0tcv extension) on a
// non-void integral-returning FunctionDecl carrying
// `[[clang::annotate("sturm::entry_point")]]`. Maps to
// MainLifecycleHitKind::EntryPointReturn — the IIFE captures the
// result into `bool __sturm_rc` and the rewritten outer function
// ends with `return __sturm_rc;`.
//
// Mirrors `main_lifecycle_basic.cpp`'s inline-sentinel posture.
#define STURM_UMBRELLA_INCLUDED 1

[[clang::annotate("sturm::entry_point")]]
bool check_invariant() {
    return true;
}
