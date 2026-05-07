// Frontend simpl. P7 (sturm-e3ru): argc/argv main_lifecycle fixture (input).
//
// Exercises the matcher_main_lifecycle for the argc/argv form
// `int main(int argc, char** argv)`. The IIFE wrapper captures by
// reference (`[&]`) so the user body's reads of `argc`/`argv` resolve
// through the lambda's enclosing scope. The KR-style alternative
// spelling `int main(int argc, char* argv[])` decays to the same
// AST type (`char**` parameter type after array-to-pointer decay),
// so this fixture covers both surfaces.
//
// The GCC-extension trailing-return form `auto main() -> int` is OUT
// OF SCOPE per PRD §5.4 / sturm-e3ru. Spec restricts to `int main(...)`.
//
// As with main_lifecycle_basic.cpp the STURM_UMBRELLA_INCLUDED sentinel
// is defined inline because the transpiler's FixedCompilationDatabase
// does not carry include paths.
#define STURM_UMBRELLA_INCLUDED 1

int main(int argc, char** argv) {
    int n = argc;
    (void)argv;
    return n - 1;
}
