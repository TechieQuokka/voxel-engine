#pragma once

#include <filesystem>
#include <string_view>

namespace mc {

/// Directory containing the running executable, resolved from /proc/self/exe.
const std::filesystem::path& executableDir();

/// Resolves a path under assets/, which is symlinked next to the executable by
/// the build. Deliberately independent of the working directory so that
/// running from any cwd, or from a debugger, behaves identically.
std::filesystem::path assetPath(std::string_view relative);

/// Reads a whole text file. Throws std::runtime_error on failure -- this is a
/// loading-boundary operation, where exceptions are permitted.
std::string readTextFile(const std::filesystem::path& path);

} // namespace mc
