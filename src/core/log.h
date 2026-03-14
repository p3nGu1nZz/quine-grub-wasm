#pragma once

#include "types.h"
#include <string>
#include <deque>
#include <vector>
#include <fstream>

// ── AppLogger ─────────────────────────────────────────────────────────────────
//
// Manages the live log ring-buffer and the immutable history ledger.
//
// File logging (optional):
//   Call init(path) once at startup.  Log entries are buffered in memory and
//   flushed to disk at most once per FLUSH_INTERVAL_MS (default 1 000 ms).
//   The buffer is always flushed unconditionally on destruction and on
//   SIGINT / SIGTERM so no entries are lost on crash or clean exit.
// ─────────────────────────────────────────────────────────────────────────────

class AppLogger {
public:
    static constexpr size_t   MAX_LOG_ENTRIES   = 1000;
    static constexpr uint64_t FLUSH_INTERVAL_MS = 1000;

    ~AppLogger();

    // Open a log file for buffered writes.  Safe to call before SDL_Init.
    void init(const std::string& logFilePath);

    // Append a new log entry.  Entries are deduplicated within 100 ms.
    // type: "info" | "success" | "warning" | "error" | "system" | "mutation"
    void log(const std::string& msg, const std::string& type = "info");

    // Append a permanent history record.
    void addHistory(const HistoryEntry& entry);

    // Write all buffered lines to disk immediately.
    void flush();

    const std::deque<LogEntry>&      logs()    const { return m_logs; }
    const std::vector<HistoryEntry>& history() const { return m_history; }

private:
    // Check elapsed time and flush if FLUSH_INTERVAL_MS has passed.
    void maybeFlush(uint64_t nowMs);

    std::deque<LogEntry>      m_logs;
    std::vector<HistoryEntry> m_history;

    // ── File-logging state ────────────────────────────────────────────────────
    std::ofstream            m_logFile;
    std::string              m_logFilePath;    // path used for locking
    std::vector<std::string> m_pendingLines;
    uint64_t                 m_lastFlushMs = 0;
    bool                     m_fileLogging = false;
};
