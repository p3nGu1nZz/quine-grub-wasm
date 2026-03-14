#include "cli.h"
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <getopt.h>

static TelemetryLevel parseTelemetryLevel(const char* v) {
    if (std::strcmp(v, "none") == 0) return TelemetryLevel::NONE;
    if (std::strcmp(v, "full") == 0) return TelemetryLevel::FULL;
    return TelemetryLevel::BASIC;
}

static MutationStrategy parseMutationStrategy(const char* v) {
    if (std::strcmp(v, "blacklist") == 0) return MutationStrategy::BLACKLIST;
    if (std::strcmp(v, "smart") == 0) return MutationStrategy::SMART;
    return MutationStrategy::RANDOM;
}

static HeuristicMode parseHeuristicMode(const char* v) {
    if (std::strcmp(v, "blacklist") == 0) return HeuristicMode::BLACKLIST;
    if (std::strcmp(v, "decay") == 0) return HeuristicMode::DECAY;
    return HeuristicMode::NONE;
}

static TelemetryFormat parseTelemetryFormat(const char* v) {
    if (std::strcmp(v, "json") == 0) return TelemetryFormat::JSON;
    return TelemetryFormat::TEXT;
}


CliOptions parseCli(int argc, char** argv) {
    CliOptions opts;
    // reset getopt state so parseCli can be called multiple times
    optind = 1;
    opterr = 0; // suppress automatic error messages

    static struct option longOpts[] = {
        {"gui",             no_argument,       nullptr, 'g'},
        {"headless",        no_argument,       nullptr, 'h'},
        {"no-gui",          no_argument,       nullptr, 'h'},
        {"nogui",           no_argument,       nullptr, 'h'},
        {"fullscreen",      no_argument,       nullptr, 'f'},
        {"windowed",        no_argument,       nullptr, 'w'},
        {"telemetry-level", required_argument, nullptr, 'l'},
        {"telemetry-dir",   required_argument, nullptr, 'd'},
        {"telemetry-format",required_argument, nullptr, 'F'},
        {"mutation-strategy",required_argument, nullptr, 'm'},
        {"heuristic",       required_argument, nullptr, 'H'},
        {"profile",         no_argument,       nullptr, 'p'},
        {"max-gen",         required_argument, nullptr, 'M'},
        {"max-run-ms",      required_argument, nullptr, 'T'},
        {"max-exec-ms",     required_argument, nullptr, 'X'},
        {"save-model",       required_argument, nullptr, 's'},
        {"load-model",       required_argument, nullptr, 'L'},
        {"kernel",          required_argument, nullptr, 'k'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    int longIndex = 0;
    const char* optString = "ghfwpl:d:F:m:H:M:TX:s:L:k:";
    while ((opt = getopt_long(argc, argv, optString, longOpts, &longIndex)) != -1) {
        switch (opt) {
            case 'g':
                opts.useGui = true;
                break;
            case 'h':
                opts.useGui = false;
                break;
            case 'f':
                opts.fullscreen = true;
                break;
            case 'w':
                opts.fullscreen = false;
                break;
            case 'l':
                if (optarg) {
                    opts.telemetryLevel = parseTelemetryLevel(optarg);
                    if ((opts.telemetryLevel == TelemetryLevel::BASIC &&
                         std::strcmp(optarg, "basic") != 0) &&
                        std::strcmp(optarg, "none") != 0 &&
                        std::strcmp(optarg, "full") != 0) {
                        std::cerr << "Warning: unknown telemetry-level '" << optarg << "'\n";
                        opts.parseError = true;
                    }
                }
                break;
            case 'd':
                if (optarg) opts.telemetryDir = optarg;
                break;
            case 'F':
                if (optarg) {
                    opts.telemetryFormat = parseTelemetryFormat(optarg);
                    if (opts.telemetryFormat == TelemetryFormat::TEXT &&
                        std::strcmp(optarg, "text") != 0) {
                        std::cerr << "Warning: unknown telemetry-format '"
                                  << optarg << "'\n";
                        opts.parseError = true;
                    }
                }
                break;
            case 'm':
                if (optarg) {
                    opts.mutationStrategy = parseMutationStrategy(optarg);
                    if (opts.mutationStrategy == MutationStrategy::RANDOM &&
                        std::strcmp(optarg, "random") != 0) {
                        std::cerr << "Warning: unknown mutation-strategy '" << optarg << "'\n";
                        opts.parseError = true;
                    }
                }
                break;
            case 'H':
                if (optarg) {
                    opts.heuristic = parseHeuristicMode(optarg);
                    if (opts.heuristic == HeuristicMode::NONE &&
                        std::strcmp(optarg, "none") != 0) {
                        std::cerr << "Warning: unknown heuristic '" << optarg << "'\n";
                        opts.parseError = true;
                    }
                }
                break;
            case 'p':
                opts.profile = true;
                break;
            case 'X':
                if (optarg) {
                    opts.maxExecMs = static_cast<int>(std::strtol(optarg, nullptr, 10));
                    if (opts.maxExecMs <= 0) {
                        std::cerr << "Warning: invalid max-exec-ms '" << optarg << "'\n";
                        opts.parseError = true;
                    }
                }
                break;
            case 'M':
                if (optarg) {
                    char* end;
                    long v = std::strtol(optarg, &end, 10);
                    if (*end != '\0' || v < 0) {
                        std::cerr << "Warning: invalid max-gen '" << optarg << "'\n";
                        opts.parseError = true;
                    } else {
                        opts.maxGen = static_cast<int>(v);
                    }
                }
                break;
            case 'T':
                if (optarg) {
                    char* end;
                    long v = std::strtol(optarg, &end, 10);
                    if (*end != '\0' || v < 0) {
                        std::cerr << "Warning: invalid max-run-ms '" << optarg << "'\n";
                        opts.parseError = true;
                    } else {
                        opts.maxRunMs = static_cast<int>(v);
                    }
                }
                break;
            case 's':
                if (optarg) opts.saveModelPath = optarg;
                break;
            case 'L':
                if (optarg) opts.loadModelPath = optarg;
                break;
            case 'k':
                if (optarg) {
                    if (std::strcmp(optarg, "seq") == 0) {
                        opts.kernelType = KernelType::SEQ;
                    } else if (std::strcmp(optarg, "glob") == 0) {
                        opts.kernelType = KernelType::GLOB;
                    } else {
                        std::cerr << "Warning: unknown kernel type '" << optarg << "'\n";
                        opts.parseError = true;
                    }
                }
                break;
            case '?':
            default:
                // getopt already prints a message; mirror it for consistency
                std::cerr << "Warning: unrecognised option '" << argv[optind-1] << "'\n";
                opts.parseError = true;
                break;
        }
    }
    return opts;
}
