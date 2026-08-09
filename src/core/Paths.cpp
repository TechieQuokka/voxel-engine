#include "core/Paths.hpp"

#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace mc {

const std::filesystem::path& executableDir() {
    static const std::filesystem::path dir = [] {
        std::error_code ec;
        const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (ec) {
            throw std::runtime_error(
                std::format("Cannot resolve /proc/self/exe: {}", ec.message()));
        }
        return exe.parent_path();
    }();
    return dir;
}

std::filesystem::path assetPath(std::string_view relative) {
    return executableDir() / "assets" / relative;
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error(std::format("Cannot open file: {}", path.string()));
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (file.bad()) {
        throw std::runtime_error(std::format("Error while reading: {}", path.string()));
    }
    return std::move(buffer).str();
}

} // namespace mc
