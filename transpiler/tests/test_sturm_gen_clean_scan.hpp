// test_sturm_gen_clean_scan.hpp -- sturm-8a2q.
//
// Pure-text scan logic factored out of `test_sturm_gen_clean.cpp` so
// the bare-spelling residue detector can be exercised in isolation by
// a sibling unit test. Header-only; the gate executable and the unit
// test both include it.
//
// Two pattern families:
//   1. QUALIFIED hits (sturm-7t85.4 G4): plain-substring scan for
//      `sturm::frontend::qint` and the two `using qint = (::)?sturm
//      ::frontend::qint` typedef spellings.
//   2. BARE hits (sturm-8a2q): residue from a wave-2 matcher arm
//      regression where `qint a[4];` (carrier-imported via
//      `using sturm::qint;`) survives unrewritten in sturm_gen output.
//      Naive `\bqint\b` would false-positive on `qint_t<W>`,
//      `qint_alias_detail`, `uncompute_*_qint`, and on the benign
//      import line itself, so we strip C/C++ comments + string
//      literals first, then accept `\bqint\b` only when the next char
//      is NOT `_` (which excludes `qint_t`, `qint_alias_*`, etc.) and
//      the line is neither a `#include` nor the `using sturm::qint`
//      import. Identifier-prefix `_qint` (e.g. `uncompute_eq_qint`)
//      is excluded by the word-boundary check (preceding char `_` is
//      a word char in C-identifier sense).

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sturm::transpile::testing {

struct Offense {
    std::filesystem::path file;
    std::size_t line = 0;
    std::string text;       // The offending line, trimmed of trailing \r/\n.
    std::string_view pat;   // Which pattern matched.
};

inline constexpr std::string_view kQualifiedPatterns[] = {
    "sturm::frontend::qint",
    "using qint = ::sturm::frontend::qint",
    "using qint = sturm::frontend::qint",
};

inline constexpr std::string_view kBarePatternLabel =
    "bare-spelling qint (declarator residue)";

// True if `c` is part of a C-identifier (alnum or `_`). Used for
// hand-rolled word-boundary checks so we do not pull in <regex>.
inline bool is_ident_char(unsigned char c) {
    return c == '_' || (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// Replace every C/C++ comment span and every "..."/'...' string span
// in `src` with spaces of equal length. Preserves byte offsets and
// line numbering so diagnostics still reference the original line.
inline std::string strip_comments_and_strings(std::string_view src) {
    std::string out(src.size(), '\0');
    const std::size_t n = src.size();
    std::size_t i = 0;
    while (i < n) {
        const char c = src[i];
        // Line comment.
        if (c == '/' && i + 1 < n && src[i + 1] == '/') {
            while (i < n && src[i] != '\n') { out[i] = ' '; ++i; }
            continue;
        }
        // Block comment.
        if (c == '/' && i + 1 < n && src[i + 1] == '*') {
            out[i] = ' '; out[i + 1] = ' '; i += 2;
            while (i + 1 < n && !(src[i] == '*' && src[i + 1] == '/')) {
                out[i] = (src[i] == '\n') ? '\n' : ' ';
                ++i;
            }
            if (i + 1 < n) { out[i] = ' '; out[i + 1] = ' '; i += 2; }
            else { while (i < n) { out[i] = ' '; ++i; } }
            continue;
        }
        // String / char literal. Honour `\"` and `\\` escapes; preserve newlines
        // so an unterminated literal does not corrupt subsequent line numbering.
        if (c == '"' || c == '\'') {
            const char quote = c;
            out[i] = ' '; ++i;
            while (i < n && src[i] != quote) {
                if (src[i] == '\\' && i + 1 < n) {
                    out[i] = ' ';
                    out[i + 1] = (src[i + 1] == '\n') ? '\n' : ' ';
                    i += 2;
                    continue;
                }
                out[i] = (src[i] == '\n') ? '\n' : ' ';
                ++i;
            }
            if (i < n) { out[i] = ' '; ++i; }
            continue;
        }
        out[i] = c;
        ++i;
    }
    return out;
}

// Trim trailing CR/LF from `s` and return as a fresh string.
inline std::string rstrip_eol(std::string_view s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return std::string(s);
}

// Scan one buffer for every pattern hit. `path` is reported verbatim
// in each Offense. The qualified-pattern scan operates on the raw
// content (so a literal substring inside a comment or string still
// fires — historical behaviour from G4); the bare-spelling scan
// operates on the comment-and-string-stripped buffer (so legitimate
// documentation referencing `qint a[4]` does not false-fire).
inline void scan_text(const std::filesystem::path& path,
                      std::string_view content,
                      std::vector<Offense>& out) {
    const std::string stripped = strip_comments_and_strings(content);
    std::size_t line_no = 1;
    std::size_t cursor  = 0;
    while (cursor <= content.size()) {
        const std::size_t eol_raw = content.find('\n', cursor);
        const std::size_t end_raw = (eol_raw == std::string::npos)
                                        ? content.size() : eol_raw;
        const std::string_view raw(content.data() + cursor, end_raw - cursor);
        const std::string_view dry(stripped.data() + cursor, end_raw - cursor);

        // Family 1: qualified-pattern substring scan on raw line. All
        // three patterns end in `qint`, so a trailing word-boundary
        // post-check excludes `sturm::frontend::qint_alias_detail`,
        // `sturm::frontend::qint_t<...>`, and similar legitimate
        // identifier-prefix uses of the namespaced spelling.
        for (std::string_view pat : kQualifiedPatterns) {
            std::size_t pos = 0;
            while ((pos = raw.find(pat, pos)) != std::string_view::npos) {
                const std::size_t after = pos + pat.size();
                const bool has_next_id = after < raw.size() &&
                    is_ident_char(static_cast<unsigned char>(raw[after]));
                if (!has_next_id) {
                    out.push_back(Offense{path, line_no, rstrip_eol(raw), pat});
                    break;  // one hit per (pattern, line) is enough.
                }
                pos = after;
            }
        }

        // Family 2: bare-spelling residue on stripped line. Skip if
        // the raw line is a `#include` directive or carries the benign
        // `using sturm::qint;` import. The import allow-list keys on
        // the dry view so a comment shaped like `using sturm::qint`
        // can never re-enable the bypass.
        const bool is_include =
            dry.find("#include") != std::string_view::npos;
        const bool is_using_qint_import =
            dry.find("using sturm::qint") != std::string_view::npos &&
            dry.find("using sturm::qint_") == std::string_view::npos;
        if (!is_include && !is_using_qint_import) {
            const std::string_view needle = "qint";
            std::size_t pos = 0;
            while ((pos = dry.find(needle, pos)) != std::string_view::npos) {
                const std::size_t after = pos + needle.size();
                const bool has_prev_id = pos > 0 &&
                    is_ident_char(static_cast<unsigned char>(dry[pos - 1]));
                const bool has_next_id = after < dry.size() &&
                    is_ident_char(static_cast<unsigned char>(dry[after]));
                if (!has_prev_id && !has_next_id) {
                    out.push_back(Offense{
                        path, line_no, rstrip_eol(raw), kBarePatternLabel});
                    break;  // one bare hit per line is enough.
                }
                pos = after;
            }
        }

        if (eol_raw == std::string::npos) break;
        cursor = eol_raw + 1;
        ++line_no;
    }
}

}  // namespace sturm::transpile::testing
