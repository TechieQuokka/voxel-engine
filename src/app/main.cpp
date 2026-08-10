#include "app/Engine.hpp"
#include "core/Log.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <span>
#include <string_view>
#include <utility>

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

    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg(args[i]);
        if (arg == "--capture" && i + 1 < args.size()) {
            options.capturePath = args[++i];
        } else if (arg == "--mesh-benchmark") {
            options.meshBenchmark = true;
        } else if (arg == "--warm-up") {
            options.warmUp = true;
        } else if (arg == "--bench-frames" && i + 1 < args.size()) {
            options.benchFrames = static_cast<mc::u32>(std::max(0, std::atoi(args[++i])));
            options.warmUp = true; // Measure the steady state, not the fill.
        } else if (arg == "--render-distance" && i + 1 < args.size()) {
            options.renderDistance = std::atoi(args[++i]);
            if (options.renderDistance < 0 || options.renderDistance > 64) {
                mc::logError("--render-distance must be between 0 and 64");
                return EXIT_FAILURE;
            }
        } else {
            mc::logError("Unknown argument: {}", arg);
            mc::logError("Usage: minecraft [--capture <path.ppm>] [--mesh-benchmark]"
                         " [--render-distance N] [--warm-up] [--bench-frames N]");
            return EXIT_FAILURE;
        }
    }

    try {
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
