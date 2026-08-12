#include "app/Engine.hpp"
#include "core/Log.hpp"
#include "worldgen/Generator.hpp"
#include "worldgen/TerrainProbe.hpp"

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

    // Handled before the engine exists, because it needs neither a window nor GL --
    // it generates terrain and counts it.
    bool probe = false;
    mc::ProbeOptions probeOptions;

    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg(args[i]);
        if (arg == "--probe") {
            probe = true;
        } else if (arg == "--probe-columns" && i + 1 < args.size()) {
            probeOptions.columns = std::atoi(args[++i]);
        } else if (arg == "--capture" && i + 1 < args.size()) {
            options.capturePath = args[++i];
        } else if (arg == "--first-person") {
            options.thirdPerson = false;
        } else if (arg == "--fly") {
            options.flying = true;
        } else if (arg == "--inventory") {
            options.openInventory = true;
        } else if (arg == "--mesh-benchmark") {
            options.meshBenchmark = true;
        } else if (arg == "--warm-up") {
            options.warmUp = true;
        } else if (arg == "--bench-seconds" && i + 1 < args.size()) {
            options.benchSeconds = std::atof(args[++i]);
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
                         " [--render-distance N] [--warm-up] [--bench-seconds S]"
                         " [--probe [--probe-columns N]] [--first-person] [--fly]"
                         " [--inventory]");
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
