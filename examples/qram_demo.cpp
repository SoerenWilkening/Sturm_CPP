#define STURM_BACKEND_ENABLED 1

#include "sturm/qram/qram_read.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/qtypes/qint_alias.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// QRAM demo — natural-syntax read `qint b = a[i];`.
//
// `add_quantum_executable()` routes this file through `sturm-transpile`,
// whose C1 matcher (`matcher_qram_subscript`) rewrites the read line into
// `sturm::qint_t<W> b; ::sturm::QRAM_read(a, i, b);` before codegen. The
// rewrite is observable: if it fires, the frontend qint's measurement
// counter stays at 0; if it does not, the converting ctor `qint(qint_t<W>)`
// runs and bumps it.
//
//   cmake --build build --target example_qram_demo --parallel 6
//   ./build/examples/example_qram_demo
//
// Inspect the rewritten source at build/sturm_gen/examples/qram_demo.cpp.

using qint = sturm::frontend::qint;

namespace {

constexpr std::size_t W = 4;
constexpr std::size_t N = 4;

template <std::size_t Width>
sturm::qint_t<Width> qcl(std::int64_t v) noexcept {
    return sturm::qint_t<Width>(v);
}

}  // namespace

int main() {
    using sturm::frontend::qint_alias_detail::measurement_count;
    using sturm::frontend::qint_alias_detail::reset_measurement_count;

    reset_measurement_count();

    std::array<sturm::qint_t<W>, N> a = {
        qcl<W>(0xA), qcl<W>(0x5), qcl<W>(0xF), qcl<W>(0x0),
    };
    qint i;
    i.phase += 3;
    qint b = a[i];
    (void)b;

    const auto m = measurement_count();
    std::printf("measurement_count = %zu  (0 => matcher rewrote `qint b = a[i];`)\n", m);
    if (m != 0u) {
        std::fprintf(stderr,
                     "FAIL: expected measurement_count == 0 but got %zu — "
                     "the C1 matcher did not rewrite `qint b = a[i];`.\n",
                     m);
        std::exit(1);
    }
    return 0;
}
