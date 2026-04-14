// io.cpp — implementation of the transpiler's filesystem helpers.

#include "sturm/transpile/io.hpp"

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace sturm::transpile {

namespace fs = std::filesystem;

fs::path resolve_output_path(const fs::path& input, const fs::path& output_dir) {
    // Absolute inputs are collapsed to their basename so the output tree
    // does not mirror the host filesystem. Relative inputs preserve their
    // subdirectory structure so the generated tree mirrors the source tree.
    if (input.is_absolute()) {
        return output_dir / input.filename();
    }
    // Manually compose: output_dir / input. `operator/` on fs::path already
    // handles embedded separators correctly, but if `input` is something
    // like `./src/foo.cpp` we normalize out the leading dot segment so the
    // result is pleasant to look at and compares correctly in tests.
    fs::path normalized = input.lexically_normal();
    return (output_dir / normalized).lexically_normal();
}

bool write_file(const fs::path& path, std::string_view bytes) {
    std::error_code ec;
    fs::path parent = path.parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent, ec);
        if (ec) return false;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

bool read_file(const fs::path& path, std::string& out) {
    out.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    // Read the whole stream into a string. Using stringstream here would
    // also work but this path is slightly more direct and does not
    // allocate an intermediate buffer.
    in.seekg(0, std::ios::end);
    std::streampos end = in.tellg();
    if (end < 0) { out.clear(); return false; }
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(end));
    if (end > 0) {
        in.read(out.data(), end);
        if (!in && !in.eof()) { out.clear(); return false; }
    }
    return true;
}

} // namespace sturm::transpile
