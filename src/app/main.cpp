#include "app/Engine.hpp"
#include "core/Log.hpp"
#include "worldgen/Generator.hpp"
#include "worldgen/TerrainProbe.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <exception>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace {

/// Parses one whole argument, or says why it could not.
///
/// **`atoi` was here, and what it does with a typo is return 0 and say nothing.**
/// `--edit 0 abc 0 air` edited y=0, `--bench-seconds foo` measured for zero seconds,
/// and both produced a run that looked deliberate. The capture and benchmark flags are
/// how this project checks its own work, so a flag that silently means something else
/// is worse here than a missing feature would be.
///
/// Trailing characters are a failure rather than a stopping point -- `16k` is a typo,
/// not sixteen -- which is why `ptr` is checked against the end.
template <typename T>
std::optional<T> parseArg(std::string_view text) {
    T value{};
    const char* const end = text.data() + text.size();
    const auto [ptr, code] = std::from_chars(text.data(), end, value);
    if (code != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

/// Reports the flag that failed and what it was given, rather than the value alone.
template <typename T>
bool readArg(std::string_view flag, std::string_view text, T& out) {
    const std::optional<T> parsed = parseArg<T>(text);
    if (!parsed.has_value()) {
        mc::logError("{} needs a number, and '{}' is not one", flag, text);
        return false;
    }
    out = *parsed;
    return true;
}

} // namespace

/// Top-level exception boundary.
///
/// Exceptions are permitted only at initialization and loading boundaries
/// (DESIGN.md 6.2). Everything that can throw -- window creation, GL loading,
/// shader compilation, asset reads -- is inside Engine's constructor, and this
/// is where those failures are turned into an exit code.
int main(int argc, char** argv) {
#ifndef NDEBUG
    mc::setLogLevel(mc::LogLevel::Debug);
#endif

    mc::Engine::Options options;

    // Handled before the engine exists, because it needs neither a window nor GL --
    // it generates terrain and counts it.
    bool probe = false;
    mc::ProbeOptions probeOptions;

    const std::span<char*> args(argv, static_cast<std::size_t>(argc));

    // **Operand count is checked separately from the flag name.** Folding it into the
    // `else if` conditions, as this loop used to, sends `--at 1 2` to the unknown-
    // argument branch and reports "Unknown argument: --at" about a flag that exists
    // and is spelled correctly. The exit code was right and the diagnostic was not.
    const auto operands = [&args](std::string_view flag, std::size_t i,
                                  std::size_t count) {
        if (i + count < args.size()) {
            return true;
        }
        mc::logError("{} needs {} value(s)", flag, count);
        return false;
    };

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg(args[i]);
        if (arg == "--probe") {
            probe = true;
        } else if (arg == "--probe-columns") {
            if (!operands(arg, i, 1)
                || !readArg(arg, args[i + 1], probeOptions.columns)) {
                return EXIT_FAILURE;
            }
            i += 1;
        } else if (arg == "--capture") {
            if (!operands(arg, i, 1)) {
                return EXIT_FAILURE;
            }
            options.capturePath = args[++i];
        } else if (arg == "--first-person") {
            options.thirdPerson = false;
        } else if (arg == "--fullscreen") {
            options.fullscreen = true;
        } else if (arg == "--fly") {
            options.flying = true;
        } else if (arg == "--at") {
            // Where the *eye* goes, not the feet -- this exists to aim a capture.
            mc::vec3 eye{};
            if (!operands(arg, i, 3) || !readArg(arg, args[i + 1], eye.x)
                || !readArg(arg, args[i + 2], eye.y)
                || !readArg(arg, args[i + 3], eye.z)) {
                return EXIT_FAILURE;
            }
            options.cameraPosition = eye;
            i += 3;
        } else if (arg == "--look") {
            mc::vec2 look{};
            if (!operands(arg, i, 2) || !readArg(arg, args[i + 1], look.x)
                || !readArg(arg, args[i + 2], look.y)) {
                return EXIT_FAILURE;
            }
            options.cameraOrientation = look;
            i += 2;
        } else if (arg == "--hold") {
            if (!operands(arg, i, 1)) {
                return EXIT_FAILURE;
            }
            options.heldItem = args[++i];
        } else if (arg == "--furnace") {
            options.openInventory = true;
            options.openFurnace = true;
        } else if (arg == "--inventory") {
            options.openInventory = true;
        } else if (arg == "--mesh-benchmark") {
            options.meshBenchmark = true;
        } else if (arg == "--warm-up") {
            options.warmUp = true;
        } else if (arg == "--bench-seconds") {
            if (!operands(arg, i, 1)
                || !readArg(arg, args[i + 1], options.benchSeconds)) {
                return EXIT_FAILURE;
            }
            i += 1;
            options.warmUp = true; // Measure the steady state, not the fill.
        } else if (arg == "--edit") {
            mc::BlockPos pos{};
            if (!operands(arg, i, 4) || !readArg(arg, args[i + 1], pos.x)
                || !readArg(arg, args[i + 2], pos.y)
                || !readArg(arg, args[i + 3], pos.z)) {
                return EXIT_FAILURE;
            }
            options.editPosition = pos;
            options.editBlock = args[i + 4];
            i += 4;
        } else if (arg == "--save-path") {
            if (!operands(arg, i, 1)) {
                return EXIT_FAILURE;
            }
            options.savePath = args[++i];
        } else if (arg == "--no-save") {
            options.noSave = true;
        } else if (arg == "--render-distance") {
            if (!operands(arg, i, 1)
                || !readArg(arg, args[i + 1], options.renderDistance)) {
                return EXIT_FAILURE;
            }
            i += 1;
            if (options.renderDistance < 0 || options.renderDistance > 64) {
                mc::logError("--render-distance must be between 0 and 64");
                return EXIT_FAILURE;
            }
        } else {
            mc::logError("Unknown argument: {}", arg);
            mc::logError("Usage: minecraft [--capture <path.ppm>] [--mesh-benchmark]"
                         " [--render-distance N] [--warm-up] [--bench-seconds S]"
                         " [--probe [--probe-columns N]] [--first-person] [--fly]"
                         " [--inventory] [--furnace] [--fullscreen]"
                         " [--hold <item>] [--at X Y Z] [--look YAW PITCH]"
                         " [--save-path <dir>] [--no-save]"
                         " [--edit X Y Z <block>]");
            return EXIT_FAILURE;
        }
    }

    try {
        if (probe) {
            const mc::Generator generator;
            mc::runTerrainProbe(generator, probeOptions);
            return EXIT_SUCCESS;
        }

        mc::Engine engine(std::move(options));
        engine.run();
    } catch (const std::exception& error) {
        mc::logError("Fatal: {}", error.what());
        return EXIT_FAILURE;
    } catch (...) {
        mc::logError("Fatal: unknown exception");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
