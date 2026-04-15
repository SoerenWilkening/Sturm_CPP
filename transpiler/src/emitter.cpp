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

// ── emit() ────────────────────────────────────────────────────────────────────

bool emit(const clang::SourceManager& sm,
          clang::Rewriter& rw,
          const std::vector<UncomputeInsertion>& insertions,
          const std::vector<QReplacement>& replacements,
          std::string_view source_path,
          std::string_view output_dir) {
    // Step 1 (PE-2): apply every source-range replacement BEFORE the
    // insertion pass. The replacement ranges (VarDecl bodies) and the
    // insertion anchor (scope `close_brace`) are disjoint by construction,
    // so Clang's Rewriter handles them independently with no merging
    // logic on our side. Replacements are applied in the order supplied
    // so the matcher can schedule them in whatever sequence is most
    // natural for its own bookkeeping.
    for (const auto& rep : replacements) {
        if (!rep.range.isValid()) {
            // Defensive: a malformed matcher output with an invalid range
            // is not representable by the Rewriter. Skip rather than abort
            // so a single bad replacement doesn't discard the whole run.
            continue;
        }
        // ReplaceText returns true on failure (unrewritable range); we
        // ignore the return so partial replacements still propagate to
        // disk. An unrewritable range is a bug elsewhere and swallowing
        // the specific record here keeps the rest of the output intact.
        (void)rw.ReplaceText(rep.range, rep.replacement);
    }

    // Step 2: apply every insertion. Reverse iteration — see file-level
    // comment for why this is correct and why forward iteration would be
    // wrong for multi-op scopes.
    for (auto it = insertions.rbegin(); it != insertions.rend(); ++it) {
        const auto& ins = *it;
        if (!ins.insert_before.isValid()) {
            // Defensive: a malformed M8 output with an invalid location is
            // not representable by the Rewriter. Skip rather than abort so
            // a single bad insertion doesn't discard the whole run.
            continue;
        }
        // InsertTextBefore puts `ins.code` directly before the anchor loc.
        // Returns true on failure (unrewritable location) — we ignore the
        // return so partial insertions still propagate to disk; an
        // unrewritable location is already a bug elsewhere and swallowing
        // the specific record here keeps the rest of the output intact.
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
        // No edits recorded (insertions empty or all filtered) — fall back
        // to the original main-file contents so we still emit a valid copy.
        clang::OptionalFileEntryRef fe = sm.getFileEntryRefForID(main_id);
        llvm::StringRef contents = sm.getBufferData(main_id);
        body.assign(contents.data(), contents.size());
        (void)fe;
    }

    // Step 4: prepend the idempotency header (helper from M5's skip module).
    std::string out;
    out.reserve(body.size() + 128);
    out.append(idempotency_header(source_path));
    out.append(body);

    // Step 5: write to <output_dir>/<resolved path>. Resolve via the same
    // helper the identity-copy path uses so the layout rules are identical.
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
