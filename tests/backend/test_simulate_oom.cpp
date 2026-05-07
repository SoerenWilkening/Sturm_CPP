// test_simulate_oom.cpp — Frontend simpl. P6: SIMULATE-mode OOM guard.
// TDD: written before implementation. Drives the inline OOM wrapper added
// to include/sturm/backend/orkan_bridge.hpp (issue sturm-ovok).
//
// Tests:
//   1. allocate_simulate(64) dies with the stable message on stderr.
//      Exact regex: 'STURM: SIMULATE mode out of memory at N qubits — reduce qubit count'
//   2. The death is via std::abort (SIGABRT on POSIX).
//   3. Small n (e.g. allocate_simulate(2)) returns normally — the guard
//      only fires on a real bad_alloc, not on every call.
//
// Death-test pattern: fork the child, redirect stderr to a pipe, read the
// captured bytes after waitpid, assert SIGABRT and substring match. This
// mirrors the existing tests/backend/test_qubit_cap.cpp helper.

#include "sturm/backend/orkan_bridge.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// POSIX fork / wait / pipe — only path supported by the project's death-test
// idiom (see test_qubit_cap.cpp).
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// ── Stable abort message sentinel ─────────────────────────────────────────────
// MUST stay in lock-step with the message in orkan_bridge.hpp.
static constexpr const char* kOomMsgPrefix =
    "STURM: SIMULATE mode out of memory at ";
static constexpr const char* kOomMsgSuffix =
    " qubits — reduce qubit count";

// ── Test 1+2: allocate_simulate(64) aborts with the stable message ───────────

static void test_allocate_simulate_64_aborts_with_message() {
    int pfd[2];
    if (pipe(pfd) < 0) { perror("pipe"); std::abort(); }

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); std::abort(); }

    if (pid == 0) {
        // Child: capture stderr, attempt the OOM-inducing allocation.
        close(pfd[0]);
        dup2(pfd[1], STDERR_FILENO);
        close(pfd[1]);

        sturm::OrkanBridge bridge;
        bridge.allocate_simulate(64u);  // must throw bad_alloc → fprintf + abort
        _exit(0);                       // unreachable on success
    }

    // Parent.
    close(pfd[1]);

    char buf[1024] = {};
    ssize_t total = 0;
    ssize_t n;
    while ((n = read(pfd[0], buf + total,
                     static_cast<size_t>(sizeof(buf) - 1) -
                         static_cast<size_t>(total))) > 0) {
        total += n;
    }
    close(pfd[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    buf[total] = '\0';

    // Assert SIGABRT (or non-zero exit) — child must NOT have exited cleanly.
    bool died = false;
    if (WIFSIGNALED(status)) died = (WTERMSIG(status) == SIGABRT);
    else if (WIFEXITED(status)) died = (WEXITSTATUS(status) != 0);
    assert(died && "allocate_simulate(64) must abort the process");

    // Assert the stable message components are present in stderr.
    bool prefix_ok = (std::strstr(buf, kOomMsgPrefix) != nullptr);
    bool suffix_ok = (std::strstr(buf, kOomMsgSuffix) != nullptr);
    assert(prefix_ok && "stderr must contain the stable OOM prefix");
    assert(suffix_ok && "stderr must contain the stable OOM suffix");
}

// ── Test 3: small n returns normally, no abort ───────────────────────────────

static void test_allocate_simulate_small_n_succeeds() {
    sturm::OrkanBridge bridge;
    bridge.allocate_simulate(2u);  // 2^2 = 4 amplitudes; trivial
    assert(bridge.num_qubits() == 2u);
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    test_allocate_simulate_64_aborts_with_message();
    test_allocate_simulate_small_n_succeeds();
    return 0;
}
