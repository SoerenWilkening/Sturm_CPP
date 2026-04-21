// Fixture TU for the PM4-8 smoke test (`pm4_smoke_dlopen`). See
// `transpiler/tests/pm4_smoke_dlopen.cpp` and its companion
// `.cmake` driver for the full contract — the commentary lives on
// the test-binary side, not here, to keep this fixture's rewritten
// buffer free of any tokens that could produce a false-positive
// match in the sentinel check.
//
// This file is compiled through
// `add_quantum_executable(pm4_smoke_dlopen_fixture ... PLUGINS
// $<TARGET_FILE:sturm-pm4-demo-plugin>)` in
// `transpiler/tests/CMakeLists.txt`. A minimal `int main() { return
// 0; }` body is sufficient: the smoke test gates on CMake +
// `plugin.cpp` + `plugin_registry.cpp` reachability, not on any
// in-tree matcher firing.

int main() { return 0; }
