// test_doc_sync.cpp — M0 doc-sync sanity checks (sturm-kdu).
// These tests verify that stale phrases from the pre-backend PRD have been
// removed from the doc files and that the replacement text is present.
//
// Strategy: open each file, read its content, and assert on string presence /
// absence.  Uses only the standard library; no external deps.

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string read_file(const char* path) {
    std::ifstream f(path);
    assert(f.is_open() && "doc file must be readable");
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// ---------------------------------------------------------------------------
// Locate the repo root relative to __FILE__ (tests/ → parent).
// __FILE__ is the absolute path used by the compiler.
// ---------------------------------------------------------------------------

static std::string repo_root() {
    std::string f = __FILE__;                    // …/tests/test_doc_sync.cpp
    auto slash = f.rfind('/');
    std::string tests_dir = f.substr(0, slash);  // …/tests
    auto slash2 = tests_dir.rfind('/');
    return tests_dir.substr(0, slash2);          // …  (repo root)
}

// ---------------------------------------------------------------------------
// Test 1 — 01_principles.md §B4
// ---------------------------------------------------------------------------
static void test_principles_b4() {
    std::string path = repo_root() + "/docs/01_principles.md";
    std::string src  = read_file(path.c_str());

    // Stale text that must be gone
    assert(!contains(src, "Four primitives.") &&
           "B4 must not still say 'Four primitives.'");
    assert(!contains(src, "prepare(q, p)") &&
           "B4 must not still list the old prepare() primitive");
    assert(!contains(src, "theta_add(q, d)") &&
           "B4 must not still list the old theta_add() primitive");
    assert(!contains(src, "xor_assign(target, control)") &&
           "B4 must not still list the old xor_assign() primitive");

    // Replacement text that must be present
    assert(contains(src, "execute_gate") &&
           "B4 must reference execute_gate after the update");
    assert(contains(src, "18") &&
           "B4 must mention 18 gates after the update");
}

// ---------------------------------------------------------------------------
// Test 2 — 04_prd_frontend.md §3 and §11
// ---------------------------------------------------------------------------
static void test_prd_frontend_nongoa() {
    std::string path = repo_root() + "/docs/04_prd_frontend.md";
    std::string src  = read_file(path.c_str());

    // The old non-goal bullets that are now superseded
    assert(!contains(src, "No uncomputation, no AND-fold of nested controls, no ancilla cursor threading through ops.") &&
           "§3 stale uncomputation non-goal must be removed");
    assert(!contains(src, "No AND-fold across nested `WHEN`s, no ancilla, no uncomputation. Documented as a stub.") &&
           "§11 stale uncomputation stub note must be removed");
}

// ---------------------------------------------------------------------------
// Test 3 — 05_spec_frontend.md ~line 349
// ---------------------------------------------------------------------------
static void test_spec_virtual_indices() {
    std::string path = repo_root() + "/docs/05_spec_frontend.md";
    std::string src  = read_file(path.c_str());

    // Stale virtual-index text must be gone
    assert(!contains(src, "virtual indices 0/1") &&
           "virtual-index encoding text must be removed from spec §11");
    assert(!contains(src, "65–128") &&
           "virtual-index range text must be removed from spec §11");

    // Replacement text must be present
    assert(contains(src, "qint.qubits[i]") &&
           "spec §11 must now reference qint.qubits[i]");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    test_principles_b4();
    test_prd_frontend_nongoa();
    test_spec_virtual_indices();
    return 0;
}
