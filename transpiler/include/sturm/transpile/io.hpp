// io.hpp — filesystem helpers for the transpiler driver (M4).
//
// The driver needs three small operations that are convenient to unit-test
// without spawning a process or pulling in LibTooling:
//
//   - resolve_output_path(input, output_dir)
//       Given a user-supplied input path (absolute or relative) and a
//       destination directory, return the full path where the transpiled
//       output for that input should be written.
//
//       * Relative input paths are mirrored under output_dir:
//             "src/foo.cpp", "/tmp/gen"  ->  "/tmp/gen/src/foo.cpp"
//       * Absolute input paths are collapsed to their basename:
//             "/usr/src/foo.cpp", "/tmp/gen"  ->  "/tmp/gen/foo.cpp"
//         (The transpiler does not mirror the host filesystem layout, only
//         the logical subtree the build is compiling from.)
//
//   - write_file(path, bytes)
//       Create parent directories if missing, then write the bytes to path.
//       Returns true on success. Binary-safe: the contents may contain NULs.
//
//   - read_file(path, out)
//       Read the entire file into `out`. Binary-safe. Returns false if the
//       file cannot be opened; in that case `out` is cleared so the caller
//       cannot accidentally use stale data.
//
// These helpers are the single point in the transpiler that touches the
// real filesystem. Later modules reuse them so we have exactly one place to
// audit for error handling, path normalization, and encoding concerns.

#ifndef STURM_TRANSPILE_IO_HPP
#define STURM_TRANSPILE_IO_HPP

#include <filesystem>
#include <string>
#include <string_view>

namespace sturm::transpile {

/// Resolve where the transpiled output for `input` should be written, under
/// `output_dir`. See file comment for the exact rules.
std::filesystem::path resolve_output_path(
    const std::filesystem::path& input,
    const std::filesystem::path& output_dir);

/// Write `bytes` to `path`, creating parent directories if missing.
/// Returns true on success, false on any filesystem or I/O error.
bool write_file(const std::filesystem::path& path, std::string_view bytes);

/// Read `path` into `out`. Returns true on success; on failure `out` is
/// cleared and false is returned.
bool read_file(const std::filesystem::path& path, std::string& out);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_IO_HPP
