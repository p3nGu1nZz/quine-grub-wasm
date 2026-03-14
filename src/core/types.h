#pragma once

#include <string>
#include <cstdint>

enum class SystemState {
    IDLE,
    BOOTING,
    LOADING_KERNEL,
    EXECUTING,
    VERIFYING_QUINE,
    SYSTEM_HALT,
    REPAIRING,
};

// Phases of the startup training sequence shown in the GUI.
enum class TrainingPhase {
    LOADING,   // animating through telemetry entries
    TRAINING,  // running gradient updates on advisor data
    COMPLETE,  // done; evolution may start
};



struct LogEntry {
    std::string id;
    uint64_t    timestamp;
    std::string message;
    // types: "info", "success", "warning", "error", "system", "mutation"
    std::string type;
};

struct HistoryEntry {
    int         generation;
    std::string timestamp;
    int         size;
    std::string action;
    std::string details;
    bool        success;
};

struct BootConfig {
    int  memorySizePages;
    bool autoReboot;
    int  rebootDelayMs;
};
