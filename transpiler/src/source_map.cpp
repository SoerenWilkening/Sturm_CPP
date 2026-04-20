// source_map.cpp — PM2-1 helper extraction for Phase-M source-map emission.
//
// Contains ONLY the `format_line_directive` free function. The helper is
// pulled out of `emitter.cpp` so small downstream TUs (the hand-built
// `test_transpile_uncompute_pass` unit test, which carries no
// `clang::Rewriter` linkage) can pick up the single symbol without
// dragging the full Rewriter/FrontendAction library tree through their
// link lines. `emitter.cpp`'s source list now omits the definition; all
// current consumers (emitter.cpp, uncompute_pass.cpp, matcher_qbool_
// compound.cpp, ...) just include `emitter.hpp` as before.
//
// Kept deliberately tiny — only `clang::Basic` headers (SourceManager,
// PresumedLoc, SourceLocation) and the standard library. Do NOT add
// Rewriter / FrontendAction / ASTContext dependencies here: the explicit
// design goal is that linking this TU adds exactly one symbol and zero
// new transitive deps beyond `libclangBasic`.

#include "sturm/transpile/emitter.hpp"

#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"

#include <filesystem>
#include <sstream>
#include <string>

namespace sturm::transpile {

// ── PM2-1: `#line` directive formatter ───────────────────────────────────────
//
// Produces `#line <N> "<file>"\n` anchored at the *presumed* location of
// `loc`, so any user-authored `#line` pragmas already in the source are
// honored. Returns an empty string on any degenerate input — invalid loc,
// invalid presumed loc, or an out-of-main-file loc — so callers can
// unconditionally concatenate the result without branching.
//
// Why presumed (not spelling)? The whole point of source maps is to make
// diagnostics and debugger frames point at what the *user* thinks of as
// "their code". If the user wrote a `#line 42 "orig.cpp"` pragma 10 lines
// up, Clang's `getPresumedLoc()` returns line 42 + offset at "orig.cpp";
// that's what a human reader expects. Spelling location would point at
// the physical file/line, defeating the pragma.
//
// Why "main file only"? Phase-M synthesizes into the rewritten main-file
// buffer; the nested plugin parse later ingests that buffer verbatim. A
// `#line` directive pointing at a header path would either be dead text
// (if the header isn't open in the nested parse's VFS) or actively
// wrong (if Clang resolves it differently than the original expansion).
// Emitting nothing is the safe fallback.
std::string format_line_directive(const clang::SourceManager& sm,
                                  clang::SourceLocation loc) {
    if (!loc.isValid()) {
        return {};
    }

    // Only emit directives for locations inside the main-file buffer.
    // `isInMainFile()` checks the *expansion* location's FileID against
    // the main FileID, which matches our "edits land in the rewritten
    // main buffer" invariant. Locations in headers, built-in buffers,
    // or the command-line scratch buffer are excluded.
    if (!sm.isInMainFile(loc)) {
        return {};
    }

    clang::PresumedLoc ploc = sm.getPresumedLoc(loc);
    if (ploc.isInvalid()) {
        return {};
    }

    // getPresumedLoc() can report line == 0 for certain degenerate
    // cases (notional "start of file" before the first token). Emitting
    // `#line 0 "..."` would tell Clang to number the next line as line
    // 1, which is fine in practice but is defensively elided here to
    // keep the output minimal and the semantics obvious.
    const unsigned line = ploc.getLine();
    if (line == 0) {
        return {};
    }

    const char* filename = ploc.getFilename();
    if (filename == nullptr) {
        return {};
    }

    // PM2-2: normalise the filename to its basename so the emitted
    // `#line` directive is reproducible across build trees. ClangTool
    // internally `makeAbsolute`s source paths before parsing (see
    // `clang/lib/Tooling/Tooling.cpp` — `ClangTool::run`), so in
    // production `getPresumedLoc().getFilename()` returns an absolute
    // path like `/abs/path/to/compound_or_and.cpp`. Baking that into
    // a snapshot fixture would break byte-exact equality across
    // machines (every CI runner has a different `$GITHUB_WORKSPACE`
    // / `$PWD`), so the PM2-1 helper extracts the basename here.
    //
    // The narrowing is safe because the helper is gated on
    // `isInMainFile(loc)`; an absolute or relative path that resolves
    // to the main file deterministically basenames to the same
    // leaf. User-authored `#line` pragmas that supply a plain
    // filename (the common case — `#line 100 "virtual.cpp"`) are
    // unaffected: a basename of a basename is itself.
    //
    // The edge case — a user pragma like `#line 100 "sub/x.cpp"` —
    // loses the `sub/` prefix. That is a conscious trade: the
    // compound matcher only emits directives whose filename is the
    // main file's own name (it never injects an unrelated path), so
    // the only way this basename step can alter a user-authored
    // `#line` directive is if the user's pragma itself lives in the
    // main file AND the pragma's target happens to share the main
    // file's FileID — a path that does not occur in any shipping
    // fixture and would be semantically ambiguous anyway.
    std::filesystem::path path(filename);
    const std::string basename = path.filename().string();
    if (basename.empty()) {
        return {};
    }

    // Build the directive. Use a stringstream so we get the same
    // conversion semantics as the rest of the emitter.
    std::ostringstream os;
    os << "#line " << line << " \"" << basename << "\"\n";
    return os.str();
}

} // namespace sturm::transpile
