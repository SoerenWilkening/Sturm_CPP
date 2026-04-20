// preamble.hpp — PM1-7 plugin-smoke-test compile preamble.
//
// The tests/smoke/minimal_or.cpp fixture is a SELF-CONTAINED black-box
// probe — it declares its own stub `sturm::qbool` + `operator|` so the
// transpiler's M7 matcher fires even without the real sturm headers on
// the include path. That stand-alone property is deliberate (see the
// fixture's file comment) but it means the POST-transpile source calls
// a free function `uncompute_or(tmp, a, b);` whose declaration is NOT
// visible anywhere in the self-contained TU.
//
// The standalone `sturm-transpile` driver dumps the rewritten source to
// disk and stops there; the release-asset smoke pipeline compiles that
// dumped file separately with the real sturm include path, where
// `sturm::uncompute_or` is declared by `<sturm/uncompute/uncompute_api.hpp>`.
//
// The plugin driver, by contrast, hands the rewritten buffer to a
// nested CompilerInvocation + EmitObjAction in the same Clang process
// that runs the matcher — so the nested parse needs a declaration of
// `uncompute_or` visible in the TU. Adding `#include <sturm/...>` to
// the fixture would break the Phase L smoke invariant that the fixture
// is self-contained; pulling the real `sturm::qbool` definition in would
// also collide with the fixture's own stub `class qbool`.
//
// Instead, this preamble is injected via `clang++ -include <preamble>`
// ahead of the fixture body. It:
//
//   1. Forward-declares `sturm::qbool` (compatible with the fixture's
//      later `class sturm::qbool { ... };` definition; a forward decl
//      of an incomplete class before the full definition is legal).
//   2. Declares the three-argument free function
//      `sturm::uncompute_or(qbool&, const qbool&, const qbool&)` so
//      ADL on the transpiler-injected `uncompute_or(tmp, a, b);` call
//      (where all three arguments have type `sturm::qbool`) finds
//      this declaration. The function is DECLARED, not defined — the
//      smoke test stops at `-c` (object emission) and never links, so
//      an undefined reference in the .o is the positive signal (the
//      call was emitted against a real symbol and is waiting for the
//      runtime library to resolve it).
//
// Keep this file tiny. Its only job is to satisfy the nested parse.

#pragma once

namespace sturm {

class qbool;

void uncompute_or(qbool& r, const qbool& a, const qbool& b);

}  // namespace sturm
