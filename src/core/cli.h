#pragma once

#include <string>

// Simple command-line option parsing used by the bootloader executable.
// Defaults are chosen so that the GUI is enabled and starts fullscreen,
// which matches the new "GUI by default" requirement.  A timeout value
// (seconds) may be supplied to automatically exit the main loop.

enum class TelemetryLevel { NONE, BASIC, FULL };
enum class TelemetryFormat { TEXT, JSON };
enum class MutationStrategy { RANDOM, BLACKLIST, SMART };
enum class HeuristicMode { NONE, BLACKLIST, DECAY };

// Select which built-in kernel the bootloader should seed evolution with.
// `GLOB` is the original self-replicating quine; `SEQ` is a tiny recurrent
// module that maintains two floats and reports them via `env.record_weight`.
enum class KernelType { GLOB, SEQ };

struct CliOptions {
    bool useGui      = true;   // false would eventually enable headless mode
    bool fullscreen  = true;   // only meaningful when useGui == true
    // new options
    TelemetryLevel telemetryLevel = TelemetryLevel::FULL;
    TelemetryFormat telemetryFormat = TelemetryFormat::TEXT;
    std::string telemetryDir;    // override export path
    MutationStrategy mutationStrategy = MutationStrategy::RANDOM;
    HeuristicMode heuristic = HeuristicMode::NONE; // NONE=no blacklist, BLACKLIST=block repeats, DECAY=block then slowly forget
    bool profile = false;
    int maxGen = 0;             // 0 = unlimited

    // model persistence paths
    std::string saveModelPath;
    std::string loadModelPath;

    // execution watchdog for entire run (milliseconds); 0 = disabled
    int maxRunMs = 0;

    // per-kernel execution timeout (milliseconds); 0 = disabled
    int maxExecMs = 0;

    // which kernel to use as the starting point
    KernelType kernelType = KernelType::GLOB;

    // flag set if any parsing error/warning occurred
    bool parseError = false;
};

// Parse arguments from main() argc/argv; unrecognised options are ignored
// (they will be passed through to the bootloader instance when exec'ing).
CliOptions parseCli(int argc, char** argv);
