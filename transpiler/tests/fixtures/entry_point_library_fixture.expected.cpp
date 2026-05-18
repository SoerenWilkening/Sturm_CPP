// entry_point_library_fixture.cpp -- sturm-0tcv fixture input.
//
// PRD §5.4 (post-sturm-0tcv): a library-level fixture function flagged
// with `[[sturm::entry_point]]` should be auto-wrapped with the
// lifecycle prologue / void IIFE / teardown. This input mirrors the
// "library fixture" canonical example referenced by the PRD §7
// follow-up entry.
//
// Expected rewrite shape (see .expected.cpp): the body of
// `library_fixture_setup()` is replaced with the
// `sturm_backend_create` prologue, a `[&]() -> void` IIFE wrapping
// the user body, and the matching `sturm_backend_destroy` teardown.
// No `return __sturm_rc;` line is emitted because the outer function
// is void.

#define STURM_UMBRELLA_INCLUDED 1

[[clang::annotate("sturm::entry_point")]]
void library_fixture_setup() {
    sturm_backend_context_t* __sturm_ctx = sturm_backend_create(STURM_MODE_DEFAULT);
    sturm_set_thread_context(__sturm_ctx);
    ([&]() -> void {
    int x = 0;
    (void)x;
})();
    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(__sturm_ctx);
}
