// test_synthesis_registry.cpp — Phase P P-B (sturm-z2e8.3) unit tests
// for `SynthesisRegistry`.
//
// The registry is a pure data structure (no AST matcher binds to it,
// unlike PI-1's `RoutineRegistry`). The test is correspondingly
// lean: we construct registries, feed them synthetic
// `const clang::FunctionDecl*` pointers (pointer identity is the
// only property the registry inspects), and assert on the observable
// API — insert / lookup round-trip, conflict with `RoutineRegistry`,
// deterministic iteration order, and null-FD guards on every public
// method.
//
// The acceptance cases from sturm-z2e8.3 are:
//
//   1. Insert, lookup, round-trip — forward/twin/adjoint_name/status
//      fields persist exactly what the caller wrote, survive re-
//      inserts, and are retrievable through both `lookup` and
//      `entries`.
//
//   2. Conflict with an existing `RoutineRegistry` entry — the
//      bridge helper `conflicts_with_routine_registry` returns true
//      iff PI-1 already has the forward, defers to it (no mutation
//      of the synthesis registry), and honours PRD §9 Q2's
//      precedence rule.
//
//   3. Deterministic iteration order across multiple inserts —
//      `entries()` and `forwards()` both yield the insertion order
//      regardless of how the underlying hash map chose to bucket
//      the keys.
//
//   4. Null-FD guard — every public method tolerates `nullptr`
//      without mutating state or crashing.
//
// Harness posture
// ---------------
// The tests use `reinterpret_cast`-cast synthetic pointer values as
// map keys; the registry never dereferences them. This mirrors the
// posture of `test_routine_registry.cpp::test_registry_api_basic_
// contract`, where the same trick isolates the data-structure
// contract from Clang's AST construction cost.
//
// No LibTooling is involved: we do not need to parse C++ to exercise
// the registry's surface. The binary links only
// `synthesis_registry.cpp` and `routine_registry.cpp` from the
// transpiler source tree — the absolute minimum needed to satisfy
// the `RoutineRegistry::contains` symbol the bridge helper calls.
// (`RoutineRegistry::contains` is header-defined and inline, but
// `RoutineRegistry`'s destructor and move-ctor live in the header
// as `= default`, so no .cpp-side symbols are actually referenced —
// the source file is included in the build target purely to keep
// any future out-of-line additions buildable without surgery here.)

#include "synthesis_registry.hpp"

#include "routine_registry.hpp"

#include <cstdio>
#include <cstdint>
#include <string>

// Forward declaration must stay at file scope so it resolves to ::clang::FunctionDecl
// rather than sturm_test_synthesis_registry_ns::clang::FunctionDecl in unity builds.
namespace clang { class FunctionDecl; }

namespace sturm_test_synthesis_registry_ns {

using sturm::transpile::RoutineRegistry;
using sturm::transpile::SynthesisEntry;
using sturm::transpile::SynthesisRegistry;
using sturm::transpile::SynthesisStatus;
using sturm::transpile::to_string;

// ── Test harness ────────────────────────────────────────────────────────────
//
// Local CHECK / CHECK_EQ_STR macros. The transpiler test suite
// standardises on a pair of free-function counters (`tests_run`,
// `tests_pass`) incremented from the macros; we re-declare them in
// this TU rather than pulling `test_matcher_harness.hpp` because
// the shared harness header declares the counters `extern` and
// depends on `matcher` / `qir` symbols this test does not otherwise
// need. A local pair keeps the test binary self-contained.
static int tests_run  = 0;
static int tests_pass = 0;

#define CHECK(cond) do {                                                 \
    ++tests_run;                                                         \
    if (cond) { ++tests_pass; }                                          \
    else {                                                               \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                        \
                     __FILE__, __LINE__, #cond);                         \
    }                                                                    \
} while (0)

#define CHECK_EQ_STR(got, want) do {                                     \
    ++tests_run;                                                         \
    if ((got) == (want)) { ++tests_pass; }                               \
    else {                                                               \
        std::fprintf(stderr, "FAIL  %s:%d  strings differ\n"             \
                             "  got:  <<<%s>>>\n"                        \
                             "  want: <<<%s>>>\n",                       \
                     __FILE__, __LINE__,                                 \
                     std::string(got).c_str(),                           \
                     std::string(want).c_str());                         \
    }                                                                    \
} while (0)

#define CHECK_FALSE(cond) CHECK(!(cond))

namespace {

// Helper to mint a synthetic `const clang::FunctionDecl*` value. The
// registry never dereferences these pointers — it only uses their
// identity as map keys — so a `reinterpret_cast` of a small integer
// is safe. Pointer values are chosen to exercise distinct hash
// buckets under every plausible `std::hash<const void*>`
// implementation.
const clang::FunctionDecl* synth_fd(std::uintptr_t tag) {
    return reinterpret_cast<const clang::FunctionDecl*>(tag);
}

// ── (1) Insert / lookup / round-trip ────────────────────────────────────────

void test_insert_creates_pending_entry() {
    // Fresh registry → empty by all queries.
    SynthesisRegistry reg;
    CHECK(reg.empty());
    CHECK(reg.size() == 0);
    CHECK_FALSE(reg.contains(nullptr));
    CHECK_FALSE(reg.contains(synth_fd(1)));
    CHECK(reg.lookup(synth_fd(1)) == nullptr);

    // Insert creates an entry with default values: forward == key,
    // twin == nullptr, adjoint_name empty, status == Pending.
    const auto* fwd_a = synth_fd(1);
    SynthesisEntry* fresh = reg.insert_forward(fwd_a);
    CHECK(fresh != nullptr);
    if (fresh) {
        CHECK(fresh->forward == fwd_a);
        CHECK(fresh->twin == nullptr);
        CHECK(fresh->adjoint_name.empty());
        CHECK(fresh->status == SynthesisStatus::Pending);
    }
    CHECK_FALSE(reg.empty());
    CHECK(reg.size() == 1);
    CHECK(reg.contains(fwd_a));

    // Lookup matches what insert returned.
    const SynthesisEntry* found = reg.lookup(fwd_a);
    CHECK(found != nullptr);
    if (found) {
        CHECK(found->forward == fwd_a);
        CHECK(found->status == SynthesisStatus::Pending);
    }
}

void test_re_insert_is_idempotent() {
    // Two calls to `insert_forward` with the same key must NOT
    // overwrite state — the second call returns the same entry
    // unchanged. This is the opposite of `RoutineRegistry::
    // insert_pair`, which overwrites; the semantics differ because
    // a reversible forward may be visited by multiple pipeline
    // stages and each stage expects previous state to persist.
    SynthesisRegistry reg;
    const auto* fwd = synth_fd(1);
    SynthesisEntry* first = reg.insert_forward(fwd);
    CHECK(first != nullptr);
    reg.set_adjoint_name(fwd, "__x_adj");
    reg.set_status(fwd, SynthesisStatus::Emitted);

    // Re-insert: fields preserved, not reset.
    SynthesisEntry* again = reg.insert_forward(fwd);
    CHECK(again != nullptr);
    if (again) {
        CHECK_EQ_STR(again->adjoint_name, std::string("__x_adj"));
        CHECK(again->status == SynthesisStatus::Emitted);
    }
    CHECK(reg.size() == 1);
}

void test_setters_round_trip_fields() {
    // Exercise each setter; verify the change propagates through
    // `lookup` and `entries`. Covers the main acceptance case (1).
    SynthesisRegistry reg;
    const auto* fwd = synth_fd(0xA1);
    const auto* twin = synth_fd(0xA2);
    reg.insert_forward(fwd);

    CHECK(reg.set_twin(fwd, twin));
    CHECK(reg.set_adjoint_name(fwd, "__marked_adj"));
    CHECK(reg.set_status(fwd, SynthesisStatus::Normalized));

    const SynthesisEntry* e = reg.lookup(fwd);
    CHECK(e != nullptr);
    if (e) {
        CHECK(e->forward == fwd);
        CHECK(e->twin == twin);
        CHECK_EQ_STR(e->adjoint_name, std::string("__marked_adj"));
        CHECK(e->status == SynthesisStatus::Normalized);
    }

    // `entries()` yields a snapshot that reflects the same state.
    auto snapshot = reg.entries();
    CHECK(snapshot.size() == 1);
    if (snapshot.size() == 1) {
        CHECK(snapshot[0].forward == fwd);
        CHECK(snapshot[0].twin == twin);
        CHECK_EQ_STR(snapshot[0].adjoint_name, std::string("__marked_adj"));
        CHECK(snapshot[0].status == SynthesisStatus::Normalized);
    }
}

void test_twin_nullptr_is_valid_state() {
    // A canonical out-param forward has `twin == nullptr`; the
    // setter must accept nullptr as a meaningful value, not
    // confuse it with "missing key".
    SynthesisRegistry reg;
    const auto* fwd = synth_fd(1);
    reg.insert_forward(fwd);
    // Populate, then explicitly clear: twin goes back to nullptr.
    reg.set_twin(fwd, synth_fd(2));
    CHECK(reg.set_twin(fwd, nullptr));
    const SynthesisEntry* e = reg.lookup(fwd);
    CHECK(e != nullptr);
    if (e) CHECK(e->twin == nullptr);
}

void test_setter_on_missing_key_returns_false() {
    // The setters' contract: false on missing key. A caller that
    // forgot to `insert_forward` first must see a false return,
    // not a silent creation.
    SynthesisRegistry reg;
    const auto* missing = synth_fd(1);
    CHECK_FALSE(reg.set_twin(missing, synth_fd(2)));
    CHECK_FALSE(reg.set_adjoint_name(missing, "x"));
    CHECK_FALSE(reg.set_status(missing, SynthesisStatus::Normalized));
    CHECK(reg.empty());
}

// ── (2) Conflict with RoutineRegistry (PRD §9 Q2) ───────────────────────────

void test_conflict_with_routine_registry_true_when_hand_registered() {
    // Hand-registered forward in `RoutineRegistry` ⇒ the synthesis
    // registry's bridge helper reports a conflict. Per PRD §9 Q2
    // the caller is expected to decline synthesis; the helper
    // itself mutates nothing.
    SynthesisRegistry syn;
    RoutineRegistry routine;
    const auto* fwd = synth_fd(0x11);

    // PI-1 has an entry for `fwd` (e.g. user wrote
    // `STURM_REGISTER_ADJOINT(fwd, manual_adj)`).
    routine.insert_pair(fwd, "manual_adj");

    // Synthesis registry has a matching candidate.
    syn.insert_forward(fwd);

    // Bridge reports the conflict.
    CHECK(syn.conflicts_with_routine_registry(fwd, routine));

    // And it must NOT mutate the synthesis entry — the status
    // stays `Pending` because the caller (not the registry) is
    // responsible for the next step.
    const SynthesisEntry* e = syn.lookup(fwd);
    CHECK(e != nullptr);
    if (e) CHECK(e->status == SynthesisStatus::Pending);
}

void test_conflict_reports_false_when_no_hand_registration() {
    // No entry in `RoutineRegistry` ⇒ no conflict, regardless of
    // the synthesis registry state.
    SynthesisRegistry syn;
    RoutineRegistry routine;
    const auto* fwd = synth_fd(0x22);
    syn.insert_forward(fwd);

    CHECK_FALSE(syn.conflicts_with_routine_registry(fwd, routine));

    // Even a deeply-progressed synthesis entry (already `Emitted`)
    // answers false when PI-1 has nothing to conflict with.
    syn.set_status(fwd, SynthesisStatus::Emitted);
    CHECK_FALSE(syn.conflicts_with_routine_registry(fwd, routine));
}

void test_conflict_query_does_not_require_synthesis_entry() {
    // A caller may want to ask the question *before* inserting the
    // forward into the synthesis registry. The bridge helper must
    // answer accurately regardless — it keys off `RoutineRegistry`
    // alone, not off the synthesis entry's existence.
    SynthesisRegistry syn;
    RoutineRegistry routine;
    const auto* fwd = synth_fd(0x33);
    routine.insert_pair(fwd, "manual_adj");

    CHECK(syn.conflicts_with_routine_registry(fwd, routine));
    CHECK(syn.empty());  // synthesis registry untouched.
}

void test_conflict_null_fd_returns_false() {
    // Null-FD guard on the bridge: nullptr reports no conflict
    // rather than e.g. "any registry-wide conflict". Pinned
    // because `RoutineRegistry::contains(nullptr)` also answers
    // false — the bridge must surface that answer verbatim.
    SynthesisRegistry syn;
    RoutineRegistry routine;
    // Even with routine_reg populated, the null-FD query is false.
    routine.insert_pair(synth_fd(1), "x");
    CHECK_FALSE(syn.conflicts_with_routine_registry(nullptr, routine));
}

// ── (3) Deterministic iteration order ───────────────────────────────────────

void test_entries_iterate_in_insertion_order() {
    // Insert three keys; `entries()` yields them in insertion order
    // regardless of hash-bucket layout. The acceptance case is
    // load-bearing for snapshot-style tests and deterministic
    // diagnostic output.
    SynthesisRegistry reg;
    const auto* fwd_a = synth_fd(0x111);
    const auto* fwd_b = synth_fd(0x222);
    const auto* fwd_c = synth_fd(0x333);
    reg.insert_forward(fwd_a);
    reg.insert_forward(fwd_b);
    reg.insert_forward(fwd_c);

    auto snap = reg.entries();
    CHECK(snap.size() == 3);
    if (snap.size() == 3) {
        CHECK(snap[0].forward == fwd_a);
        CHECK(snap[1].forward == fwd_b);
        CHECK(snap[2].forward == fwd_c);
    }

    // `forwards()` mirrors the same order.
    auto keys = reg.forwards();
    CHECK(keys.size() == 3);
    if (keys.size() == 3) {
        CHECK(keys[0] == fwd_a);
        CHECK(keys[1] == fwd_b);
        CHECK(keys[2] == fwd_c);
    }
}

void test_re_insert_does_not_reorder() {
    // Re-inserting an existing key must NOT move it to the end of
    // the iteration order. The deterministic-order contract says
    // "insertion order", and re-insertion is not a new insertion.
    SynthesisRegistry reg;
    const auto* fwd_a = synth_fd(1);
    const auto* fwd_b = synth_fd(2);
    reg.insert_forward(fwd_a);
    reg.insert_forward(fwd_b);
    reg.insert_forward(fwd_a);  // re-insert — should stay first.

    auto keys = reg.forwards();
    CHECK(keys.size() == 2);
    if (keys.size() == 2) {
        CHECK(keys[0] == fwd_a);
        CHECK(keys[1] == fwd_b);
    }
}

void test_iteration_order_survives_mutations() {
    // Mutating a middle entry must not shuffle it in the iteration
    // order. The parallel-vector scheme makes this automatic — we
    // pin the guarantee here because a future refactor to a single
    // `std::map<>`-backed implementation would break it silently.
    SynthesisRegistry reg;
    const auto* fwd_a = synth_fd(0xAA);
    const auto* fwd_b = synth_fd(0xBB);
    const auto* fwd_c = synth_fd(0xCC);
    reg.insert_forward(fwd_a);
    reg.insert_forward(fwd_b);
    reg.insert_forward(fwd_c);

    reg.set_twin(fwd_b, synth_fd(0xBB'00));
    reg.set_adjoint_name(fwd_b, "__b_adj");
    reg.set_status(fwd_b, SynthesisStatus::Emitted);

    auto keys = reg.forwards();
    CHECK(keys.size() == 3);
    if (keys.size() == 3) {
        CHECK(keys[0] == fwd_a);
        CHECK(keys[1] == fwd_b);
        CHECK(keys[2] == fwd_c);
    }
    // And the entry's fields reflect the mutations.
    auto snap = reg.entries();
    if (snap.size() == 3) {
        CHECK(snap[1].forward == fwd_b);
        CHECK(snap[1].twin != nullptr);
        CHECK_EQ_STR(snap[1].adjoint_name, std::string("__b_adj"));
        CHECK(snap[1].status == SynthesisStatus::Emitted);
    }
}

// ── (4) Null-FD guard across the whole public surface ───────────────────────

void test_null_fd_guard_on_every_public_method() {
    // Every method that takes a `const clang::FunctionDecl*` must
    // treat nullptr as a silent no-op or false answer — never a
    // crash, never a phantom insert.
    SynthesisRegistry reg;

    CHECK(reg.insert_forward(nullptr) == nullptr);
    CHECK(reg.empty());
    CHECK_FALSE(reg.contains(nullptr));
    CHECK(reg.lookup(nullptr) == nullptr);
    CHECK_FALSE(reg.set_twin(nullptr, synth_fd(1)));
    CHECK_FALSE(reg.set_adjoint_name(nullptr, "x"));
    CHECK_FALSE(reg.set_status(nullptr, SynthesisStatus::Emitted));

    // Still empty after the null-FD gauntlet.
    CHECK(reg.empty());
    CHECK(reg.size() == 0);
    CHECK(reg.entries().empty());
    CHECK(reg.forwards().empty());
}

// ── Supporting coverage ────────────────────────────────────────────────────

void test_status_to_string_stable() {
    // The spellings are part of the contract (diagnostic text, test
    // matching). A regression here would silently break any caller
    // that stringifies the status.
    CHECK_EQ_STR(std::string(to_string(SynthesisStatus::Pending)),
                 std::string("pending"));
    CHECK_EQ_STR(std::string(to_string(SynthesisStatus::Normalized)),
                 std::string("normalized"));
    CHECK_EQ_STR(std::string(to_string(SynthesisStatus::Emitted)),
                 std::string("emitted"));
    CHECK_EQ_STR(std::string(to_string(SynthesisStatus::Failed)),
                 std::string("failed"));
}

void test_clear_resets_state() {
    // `clear()` is a test-only convenience — must empty both the
    // map and the insertion-order vector.
    SynthesisRegistry reg;
    reg.insert_forward(synth_fd(1));
    reg.insert_forward(synth_fd(2));
    CHECK(reg.size() == 2);
    reg.clear();
    CHECK(reg.empty());
    CHECK(reg.size() == 0);
    CHECK(reg.entries().empty());
    CHECK(reg.forwards().empty());
}

void test_multiple_entries_independent_state() {
    // Two entries must carry independent field values. Pins that
    // the underlying map really stores per-key state (a regression
    // that aliases all entries would pass the single-entry tests).
    SynthesisRegistry reg;
    const auto* a = synth_fd(0xA);
    const auto* b = synth_fd(0xB);
    reg.insert_forward(a);
    reg.insert_forward(b);
    reg.set_adjoint_name(a, "__a_adj");
    reg.set_adjoint_name(b, "__b_adj");
    reg.set_status(a, SynthesisStatus::Emitted);
    reg.set_status(b, SynthesisStatus::Failed);

    const SynthesisEntry* ea = reg.lookup(a);
    const SynthesisEntry* eb = reg.lookup(b);
    CHECK(ea != nullptr);
    CHECK(eb != nullptr);
    if (ea && eb) {
        CHECK_EQ_STR(ea->adjoint_name, std::string("__a_adj"));
        CHECK_EQ_STR(eb->adjoint_name, std::string("__b_adj"));
        CHECK(ea->status == SynthesisStatus::Emitted);
        CHECK(eb->status == SynthesisStatus::Failed);
    }
}

} // namespace

}  // namespace sturm_test_synthesis_registry_ns

int run_test_synthesis_registry(int /*argc*/, char** /*argv*/) {
    using namespace sturm_test_synthesis_registry_ns;
    using sturm_test_synthesis_registry_ns::tests_run;
    using sturm_test_synthesis_registry_ns::tests_pass;
    test_insert_creates_pending_entry();
    test_re_insert_is_idempotent();
    test_setters_round_trip_fields();
    test_twin_nullptr_is_valid_state();
    test_setter_on_missing_key_returns_false();
    test_conflict_with_routine_registry_true_when_hand_registered();
    test_conflict_reports_false_when_no_hand_registration();
    test_conflict_query_does_not_require_synthesis_entry();
    test_conflict_null_fd_returns_false();
    test_entries_iterate_in_insertion_order();
    test_re_insert_does_not_reorder();
    test_iteration_order_survives_mutations();
    test_null_fd_guard_on_every_public_method();
    test_status_to_string_stable();
    test_clear_resets_state();
    test_multiple_entries_independent_state();

    std::fprintf(stderr,
                 "test_synthesis_registry: %d / %d checks passed\n",
                 tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
