// Frontend simpl. P7 (sturm-e3ru): idempotency fixture (input).
//
// Already-rewritten source: re-running the transpiler MUST be a no-op.
// The matcher detects "already rewritten" by probing for the
// `__sturm_ctx` VarDecl in main's first compound statement (the
// canonical first line of the IIFE form per PRD §5.4 listing).
//
// Expected output: byte-identical to input modulo whitespace; the
// snapshot harness uses `compare_files` (strict byte equality) so the
// expected file is the input plus the auto-generated header.
#define STURM_UMBRELLA_INCLUDED 1

// Forward-declared lifecycle ABI shims. The transpile pass runs with
// a FixedCompilationDatabase that has no include paths, so the type
// resolution / call lookup for `sturm_backend_create` etc. needs an
// in-source declaration. Real user code gets these from `sturm.h`.
struct sturm_backend_context_t;
extern "C" sturm_backend_context_t* sturm_backend_create(int);
extern "C" void sturm_set_thread_context(sturm_backend_context_t*);
extern "C" void sturm_backend_destroy(sturm_backend_context_t*);
#define STURM_MODE_DEFAULT 1

int main() {
    sturm_backend_context_t* __sturm_ctx = sturm_backend_create(STURM_MODE_DEFAULT);
    sturm_set_thread_context(__sturm_ctx);
    int __sturm_rc = ([&]() -> int {
    int x = 42;
    (void)x;
    return 0;
})();
    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(__sturm_ctx);
    return __sturm_rc;
}
