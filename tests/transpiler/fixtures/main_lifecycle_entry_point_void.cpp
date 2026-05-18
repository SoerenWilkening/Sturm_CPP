// sturm-0tcv: entry_point attribute snapshot — void library fixture.
//
// Exercises the matcher_main_lifecycle (sturm-0tcv extension) on a
// void-returning FunctionDecl carrying `[[clang::annotate(
// "sturm::entry_point")]]`. The matcher must:
//
//   1. Detect the `AnnotateAttr` carrier and gate on the
//      `sturm::entry_point` annotation string.
//   2. Confirm STURM_UMBRELLA_INCLUDED is defined; STURM_NO_AUTO_
//      LIFECYCLE is NOT defined.
//   3. Confirm the subject is a free FunctionDecl (not CXXMethodDecl)
//      with a void return type.
//   4. Rewrite the body into the EntryPointVoid IIFE form (PRD §5.4
//      post-sturm-0tcv listing): no `int __sturm_rc` capture and no
//      `return __sturm_rc;` after teardown.
//
// Mirrors `main_lifecycle_basic.cpp`'s inline-sentinel posture for
// the same reason: the transpiler runs with a FixedCompilationDatabase
// that has no include paths.
#define STURM_UMBRELLA_INCLUDED 1

[[clang::annotate("sturm::entry_point")]]
void library_fixture() {
    int x = 42;
    (void)x;
}
