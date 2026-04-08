// test_qubit_cap.cpp — M18: 17-qubit cap enforcement on QubitPool.
// TDD: written before implementation.
//
// Tests:
//   1. acquire() for 17 qubits succeeds (no abort).
//   2. acquire() for the 18th qubit triggers std::abort (death test via fork).
//   3. allocate 17, release 1, acquire 1 — succeeds (recycled slot reused).
//   4. The abort message printed to stderr contains the stable sentinel string.

#include "sturm/core/qubit_pool.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// POSIX fork/wait for death testing.
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// ── Stable abort message sentinel ─────────────────────────────────────────────
// Any test that checks the message text must match this constant.
static constexpr const char* kAbortMsg =
    "STURM: qubit cap exceeded (max 17)";

// ── Helper: run a lambda in a child process; assert it exits abnormally ────────
//
// Returns true if the child was killed by SIGABRT (or exited non-zero via
// abort()), false otherwise.  Only available on POSIX.
template <typename F>
static bool dies_with_abort(F&& fn) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        std::abort();
    }
    if (pid == 0) {
        // Child: run the function; if it doesn't abort, exit normally.
        fn();
        _exit(0);
    }
    // Parent: wait and inspect exit status.
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status)) {
        return WTERMSIG(status) == SIGABRT;
    }
    // abort() may also raise SIGABRT which sets WIFSIGNALED, but on some
    // platforms it exits with a non-zero code.
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status) != 0;
    }
    return false;
}

// ── Test 1: 17 successive acquire() calls all succeed ─────────────────────────

static void test_acquire_17_succeeds() {
    sturm::QubitPool pool(17u);

    std::vector<int> indices;
    indices.reserve(17);
    for (int i = 0; i < 17; ++i) {
        int idx = pool.acquire();
        assert(idx >= 0);
        indices.push_back(idx);
    }
    // in_use() must equal 17.
    assert(pool.in_use() == 17);

    // Release all so the pool destructs cleanly.
    for (int idx : indices) pool.release(idx);
}

// ── Test 2: 18th acquire() triggers std::abort ────────────────────────────────

static void test_acquire_18th_aborts() {
    bool aborted = dies_with_abort([]() {
        sturm::QubitPool pool(17u);
        for (int i = 0; i < 17; ++i) {
            pool.acquire();  // consume the full budget
        }
        // This must abort.
        pool.acquire();
    });
    assert(aborted && "18th acquire() must trigger std::abort");
}

// ── Test 3: allocate 17, release 1, acquire 1 succeeds ────────────────────────
//
// Verifies that the free-list recycles an index so a release+acquire pair
// within budget never aborts.

static void test_release_then_acquire_succeeds() {
    sturm::QubitPool pool(17u);

    std::vector<int> indices;
    indices.reserve(17);
    for (int i = 0; i < 17; ++i) {
        indices.push_back(pool.acquire());
    }
    assert(pool.in_use() == 17);

    // Release one.
    int released = indices.back();
    indices.pop_back();
    pool.release(released);
    assert(pool.in_use() == 16);

    // Now acquire one more — must succeed, not abort.
    int recycled = pool.acquire();
    assert(recycled >= 0);
    assert(pool.in_use() == 17);
    indices.push_back(recycled);

    // Clean up.
    for (int idx : indices) pool.release(idx);
}

// ── Test 4: abort message contains the stable sentinel ────────────────────────
//
// Runs the overflow scenario in a child process with stderr redirected to a
// pipe; the parent reads and checks for the sentinel string.

static void test_abort_message_stable() {
    // Create a pipe to capture child's stderr.
    int pfd[2];
    if (pipe(pfd) < 0) { perror("pipe"); std::abort(); }

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); std::abort(); }

    if (pid == 0) {
        // Child: redirect stderr to write-end of pipe.
        close(pfd[0]);
        dup2(pfd[1], STDERR_FILENO);
        close(pfd[1]);

        sturm::QubitPool pool(17u);
        for (int i = 0; i < 17; ++i) pool.acquire();
        pool.acquire();  // should abort after writing message
        _exit(0);        // unreachable
    }

    // Parent.
    close(pfd[1]);

    char buf[512] = {};
    ssize_t total = 0;
    ssize_t n;
    while ((n = read(pfd[0], buf + total,
                     static_cast<size_t>(sizeof(buf) - 1) - static_cast<size_t>(total))) > 0) {
        total += n;
    }
    close(pfd[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    buf[total] = '\0';
    bool found = (std::strstr(buf, kAbortMsg) != nullptr);
    assert(found && "abort message must contain the stable sentinel string");
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    test_acquire_17_succeeds();
    test_acquire_18th_aborts();
    test_release_then_acquire_succeeds();
    test_abort_message_stable();
    return 0;
}
