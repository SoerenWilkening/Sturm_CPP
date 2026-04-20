// emitter.cpp — implementation of the M9 C++ emitter.
//
// Flow
// ----
// 1. For each UncomputeInsertion in the list, call InsertTextBefore on the
//    Rewriter. We iterate in REVERSE of M8's output order; see the "Why
//    reverse iteration?" block below for the Rewriter-semantics reasoning.
// 2. Ask the Rewriter for the main file's buffer and serialize it into a
//    std::string via raw_string_ostream.
// 3. Prepend the two-line idempotency header so the transpiler can
//    recognize its own output on a subsequent run (PRD AC #5). The header
//    builder lives in M5's skip module so emit() depends on skip.hpp.
// 4. Write the final bytes to disk via the M4 io helpers, mirroring the
//    source subtree under `output_dir` exactly as the identity copy path
//    in main.cpp already does for non-transpiled files.
//
// Why reverse iteration?
// ----------------------
// M8 hands us insertions in LIFO order: for a scope with forward ops
// [op0, op1, ..., opN] the list is [opN_ins, ..., op1_ins, op0_ins]. The
// reason is correctness — at runtime, the most recently computed
// intermediate must be uncomputed first.
//
// clang::Rewriter::InsertTextBefore places NEW text before any other text
// previously inserted at the same SourceLocation. So if we iterated the
// M8 list in forward order:
//
//     InsertTextBefore(close_brace, "uncompute opN")   // buffer: ...opN}
//     InsertTextBefore(close_brace, "uncompute opN-1") // buffer: ...opN-1 opN}
//     ...
//     InsertTextBefore(close_brace, "uncompute op0")   // buffer: ...op0 op1 ... opN}
//
// the final source-text order would be `op0, op1, ..., opN` — forward
// order, which is the opposite of what uncomputation requires.
//
// Iterating M8's list in REVERSE fixes this:
//
//     InsertTextBefore(close_brace, "uncompute op0")   // buffer: ...op0}
//     InsertTextBefore(close_brace, "uncompute op1")   // buffer: ...op1 op0}
//     ...
//     InsertTextBefore(close_brace, "uncompute opN")   // buffer: ...opN ... op1 op0}
//
// Final source order: `opN, ..., op1, op0` — reverse of forward, which is
// the exact LIFO execution order the uncompute pass is built around.
//
// For the MVP single-op-per-scope case this distinction is invisible;
// the reverse iteration is a future-proofing step so the moment M8 feeds
// us two ops in one scope the test immediately catches an ordering bug.

#include "sturm/transpile/emitter.hpp"

#include "sturm/transpile/io.hpp"
#include "sturm/transpile/skip.hpp"
#include "sturm/transpile/uncompute_pass.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/Basic/FileEntry.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Rewrite/Core/RewriteBuffer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

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
    // conversion semantics as the rest of the emitter (which already
    // depends on <sstream>).
    std::ostringstream os;
    os << "#line " << line << " \"" << basename << "\"\n";
    return os.str();
}

namespace {

// Shared core: apply the (replacements, insertions) pair to the Rewriter in
// the PE-2 order (replacements first, insertions-in-reverse second) and
// return the serialized main-file buffer. No header, no file I/O.
//
// Both emit() and emit_to_string() funnel through here so the byte-level
// rewrite semantics are defined exactly once. The back-compat emit()
// overload keeps its historical signature (SourceManager + externally-owned
// Rewriter) so existing call sites and tests stay byte-identical; the new
// emit_to_string() constructs its own Rewriter from ASTContext because the
// PM1-4 plugin never has one to hand us.
std::string apply_rewrites_and_serialize(
    const clang::SourceManager& sm,
    clang::Rewriter& rw,
    const std::vector<UncomputeInsertion>& insertions,
    const std::vector<QReplacement>& replacements) {
    // Step 1 (PE-2): apply every source-range replacement BEFORE the
    // insertion pass. The replacement ranges (VarDecl bodies) and the
    // insertion anchor (scope `close_brace`) are disjoint by construction,
    // so Clang's Rewriter handles them independently with no merging
    // logic on our side.
    for (const auto& rep : replacements) {
        if (!rep.range.isValid()) {
            continue;
        }
        (void)rw.ReplaceText(rep.range, rep.replacement);
    }

    // Step 2: apply every insertion. Reverse iteration — see file-level
    // comment for why this is correct for multi-op scopes.
    for (auto it = insertions.rbegin(); it != insertions.rend(); ++it) {
        const auto& ins = *it;
        if (!ins.insert_before.isValid()) {
            continue;
        }
        (void)rw.InsertTextBefore(ins.insert_before, ins.code);
    }

    // Step 3: serialize the rewritten main-file buffer into a string.
    clang::FileID main_id = sm.getMainFileID();
    const clang::RewriteBuffer* buf = rw.getRewriteBufferFor(main_id);

    std::string body;
    if (buf) {
        llvm::raw_string_ostream os(body);
        buf->write(os);
        os.flush();
    } else {
        // No edits recorded — fall back to the original main-file contents
        // so callers get the unmodified TU text rather than an empty string.
        llvm::StringRef contents = sm.getBufferData(main_id);
        body.assign(contents.data(), contents.size());
    }
    return body;
}

} // namespace

// ── emit_to_string() ─────────────────────────────────────────────────────────
//
// PM1-2: pure rewrite. Synthesize insertions/replacements from the QUnit,
// build a Rewriter against `ctx`, apply the edits, return the serialized
// main-file buffer. No idempotency header is prepended — the PM1-4 plugin
// feeds this buffer to a nested CompilerInvocation that parses it as the
// original TU, so the sentinel would be out-of-place.
std::string emit_to_string(const QUnit& unit, clang::ASTContext& ctx) {
    auto synth = sturm::transpile::synthesize(unit);
    clang::Rewriter rw(ctx.getSourceManager(), ctx.getLangOpts());
    return apply_rewrites_and_serialize(
        ctx.getSourceManager(), rw, synth.insertions, synth.replacements);
}

// ── emit_to_file() ───────────────────────────────────────────────────────────
//
// PM1-2: call emit_to_string, prepend the idempotency header, write to the
// resolved output path. The standalone driver (main.cpp) calls this; the
// plugin (PM1-4) does NOT — it calls emit_to_string and routes the bytes
// directly into a nested invocation without a header.
bool emit_to_file(const QUnit& unit,
                  clang::ASTContext& ctx,
                  std::string_view source_path,
                  std::string_view output_dir) {
    std::string body = emit_to_string(unit, ctx);

    // Prepend the idempotency header (helper from M5's skip module).
    std::string out;
    out.reserve(body.size() + 128);
    out.append(idempotency_header(source_path));
    out.append(body);

    // Write to <output_dir>/<resolved path>. Resolve via the same helper
    // the identity-copy path uses so the layout rules are identical.
    fs::path dst = sturm::transpile::resolve_output_path(
        fs::path(source_path), fs::path(output_dir));
    if (!sturm::transpile::write_file(dst, out)) {
        std::fprintf(stderr,
                     "sturm-transpile: error: could not write output %s\n",
                     dst.string().c_str());
        return false;
    }
    return true;
}

// ── emit() ────────────────────────────────────────────────────────────────────

bool emit(const clang::SourceManager& sm,
          clang::Rewriter& rw,
          const std::vector<UncomputeInsertion>& insertions,
          const std::vector<QReplacement>& replacements,
          std::string_view source_path,
          std::string_view output_dir) {
    // Shared core applies replacements, then insertions in reverse, and
    // serializes the main-file buffer.
    std::string body = apply_rewrites_and_serialize(
        sm, rw, insertions, replacements);

    // Prepend the idempotency header (helper from M5's skip module).
    std::string out;
    out.reserve(body.size() + 128);
    out.append(idempotency_header(source_path));
    out.append(body);

    // Write to <output_dir>/<resolved path>. Resolve via the same helper
    // the identity-copy path uses so the layout rules are identical.
    fs::path dst = sturm::transpile::resolve_output_path(
        fs::path(source_path), fs::path(output_dir));
    if (!sturm::transpile::write_file(dst, out)) {
        std::fprintf(stderr,
                     "sturm-transpile: error: could not write output %s\n",
                     dst.string().c_str());
        return false;
    }
    return true;
}

// ── Back-compat overload ─────────────────────────────────────────────────────
//
// PE-2: pre-Phase-E call sites (the MVP driver, the emitter test harness,
// and any external integrator) pass only an insertion list. Rather than
// churn every such site with an explicit empty-vector argument, we forward
// through the five-argument form with an empty replacement list. The
// semantics match the old four-argument emit() bit-for-bit, which is what
// the "byte-identical existing snapshot fixtures" acceptance criterion
// requires.
bool emit(const clang::SourceManager& sm,
          clang::Rewriter& rw,
          const std::vector<UncomputeInsertion>& insertions,
          std::string_view source_path,
          std::string_view output_dir) {
    static const std::vector<QReplacement> kEmpty{};
    return emit(sm, rw, insertions, kEmpty, source_path, output_dir);
}

} // namespace sturm::transpile
