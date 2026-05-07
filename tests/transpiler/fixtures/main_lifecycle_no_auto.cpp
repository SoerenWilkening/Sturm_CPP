// Frontend simpl. P7 (sturm-e3ru): STURM_NO_AUTO_LIFECYCLE escape hatch
// (input). Drives PRD acceptance criterion A6.
//
// When STURM_NO_AUTO_LIFECYCLE is defined BEFORE the umbrella sentinel,
// the matcher_main_lifecycle MUST skip the rewrite — the user has opted
// into the explicit lifecycle form. The expected output is byte-identical
// to the input modulo whitespace; for this snapshot test the harness
// uses `compare_files` which is strict-byte, so the expected file is
// literally the input plus the auto-generated header (no body rewrite).
//
// Same structural shape as main_lifecycle_basic.cpp but with the escape
// hatch macro defined.
#define STURM_NO_AUTO_LIFECYCLE 1
#define STURM_UMBRELLA_INCLUDED 1

int main() {
    int x = 42;
    (void)x;
    return 0;
}
