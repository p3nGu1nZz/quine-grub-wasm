#pragma once

#include "types.h"
#include "wasm/parser.h"
#include <string>
#include <vector>
#include <deque>

// ── Exporter ──────────────────────────────────────────────────────────────────
//
// Produces a human-readable telemetry report from the current simulation state.
// ─────────────────────────────────────────────────────────────────────────────

struct ExportData {
    int                        generation;
    std::string                currentKernel;   // base64
    std::vector<Instruction>   instructions;
    std::deque<LogEntry>       logs;
    std::vector<HistoryEntry>  history;

    // telemetry metrics (optional)
    int mutationsAttempted = 0;
    int mutationsApplied   = 0;
    int mutationInsert     = 0;
    int mutationDelete     = 0;
    int mutationModify     = 0;
    int mutationAdd        = 0;
    std::string trapCode;
    double genDurationMs   = 0.0;
    int kernelSizeMin      = 0;
    int kernelSizeMax      = 0;
    // number of entries currently held in the heuristic blacklist
    int heuristicBlacklistCount = 0;
    int advisorEntryCount = 0;  // number of entries loaded by Advisor

    // if multi-instance support is active, the current set of base64
    // kernels that have been spawned and not yet killed.
    std::vector<std::string> instances;
};

// Build a full text report (hex dump, disassembly, history) from the given data.
std::string buildReport(const ExportData& data);
