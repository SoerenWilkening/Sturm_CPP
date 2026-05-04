// render_qint_typename.hpp — sturm-65rs.7 (Beat C0).
//
// Single source of truth for the rendered ancilla / result-type
// spelling used by every `render_*` emitter family in the transpiler:
//
//   * QRAM emission         (qram_emitter.cpp / qram_emitter_expr.cpp)
//   * Lossy OOP rewrites    (lossy_rewrite_emitter.cpp)
//   * Modular rewrites      (modular_rewrite_emitter.cpp)
//
// PRD §4.3 close, plan §8 (sturm-qac.7).
//
// Contract
// --------
// `W > 0` ⇒ `sturm::qint_t<W>` so the emitted text compiles in TUs
// without a `using qint = ...;` typedef. `W == 0` falls back to the
// legacy unqualified `qint` typename for hermetic-stub fixtures —
// matches the sturm-czfi posture the family adopted before the
// extraction.
//
// One inline definition. Header-only. Zero LibTooling / Clang
// dependencies — just <sstream>/<string> — so the test target can
// exercise it without paying for any LLVM/Clang link cost.

#pragma once

#include <sstream>
#include <string>

namespace sturm::transpile {

// Pure-string emission shape. `W == 0` ⇒ `"qint"` (legacy hermetic
// fixtures); `W > 0` ⇒ `"sturm::qint_t<W>"` (the production target
// shape, resolves to a concrete type even when the example TU has no
// `using qint = ...;` typedef).
inline std::string render_qint_typename(unsigned W) {
    if (W == 0) return "qint";
    std::ostringstream os;
    os << "sturm::qint_t<" << W << ">";
    return os.str();
}

} // namespace sturm::transpile
