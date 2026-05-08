// QRAM demo — natural-syntax read `qint b = a[i];`, with the resulting
// gate stream printed as an ASCII circuit diagram.
//
// `add_quantum_executable()` routes this file through `sturm-transpile`,
// whose C1 matcher (`matcher_qram_subscript`) rewrites the read line into
// `sturm::qint_t<W> b; ::sturm::QRAM_read(a, i, b);` before codegen, and
// whose `matcher_main_lifecycle` (Phase 7 / sturm-e3ru) wraps `main` with
// the auto-injected `sturm_backend_create` / `sturm_backend_destroy`
// lifecycle now that this TU includes the umbrella `sturm.h`.
//
// Frontend simplification (PRD §4 "After" listing — sturm-yggr / Phase 8):
// the umbrella `sturm.h` brings in the curated public API
// (`qint`, `qbool`, the C ABI lifecycle), the opt-in `sturm/qram.h` makes
// the QRAM_read overloads reachable, and the opt-in `sturm/draw_ascii.h`
// exposes the no-arg renderer entry points (PRD §5.6).
#include "sturm.h"
#include "sturm/draw_ascii.h"
#include "sturm/qram.h"

int main() {
    qint a[4];
    for (int i = 0; i < 4; ++i) {
        a[i] = i;
    }

    qint i = 2;
    i[0].phi() += 3;
    i[1].phi() += 3;
    qint b = a[i];

    std::fputs("\n--- QRAM read circuit (APPEND-mode IR) ---\n", stdout);
    sturm::print_ascii();
    std::fprintf(stdout, "\n[gate count = %zu]\n", sturm::gate_count());
    return 0;
}
