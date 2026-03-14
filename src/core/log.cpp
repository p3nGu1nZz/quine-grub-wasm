#include "log.h"
#include "util.h"

#include <SDL3/SDL.h>

#include <csignal>
#include <sstream>
#include <iomanip>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

// ── Global pointer for signal/atexit handler ──────────────────────────────────
static AppLogger* s_loggerInstance = nullptr;

static void flushSignalHandler(int /*sig*/) {
    if (s_loggerInstance) s_loggerInstance->flush();
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

AppLogger::~AppLogger() {
    flush();
    if (m_logFile.is_open()) m_logFile.close();
    if (s_loggerInstance == this) s_loggerInstance = nullptr;
}

void AppLogger::init(const std::string& logFilePath) {
    m_logFilePath = logFilePath;
    m_logFile.open(logFilePath, std::ios::app);
    if (!m_logFile.is_open()) return;

    m_fileLogging = true;
    m_lastFlushMs = static_cast<uint64_t>(SDL_GetTicks());

    // Register global pointer so signal/atexit handlers can reach us.
    s_loggerInstance = this;
    std::signal(SIGINT,  flushSignalHandler);
    std::signal(SIGTERM, flushSignalHandler);

    // Write session header.
    m_logFile << "=== Session started " << nowIso() << " ===\n";
    m_logFile.flush();
}

// ── Logging ───────────────────────────────────────────────────────────────────

void AppLogger::log(const std::string& msg, const std::string& type) {
    uint64_t t = static_cast<uint64_t>(SDL_GetTicks());

    // Deduplicate within 100 ms
    if (!m_logs.empty()) {
        const auto& last = m_logs.back();
        if (last.message == msg && (t - last.timestamp) < 100)
            return;
    }

    m_logs.push_back({ randomId(), t, msg, type });
    if (m_logs.size() > MAX_LOG_ENTRIES)
        m_logs.pop_front();

    if (m_fileLogging) {
        // Format: [<ms>] [TYPE] message
        std::ostringstream ss;
        ss << '[' << std::setw(10) << std::setfill('0') << t
           << "] [" << type << "] " << msg;
        m_pendingLines.push_back(ss.str());
        maybeFlush(t);
    }
}

void AppLogger::addHistory(const HistoryEntry& entry) {
    m_history.push_back(entry);
}

// ── Flush helpers ─────────────────────────────────────────────────────────────

void AppLogger::maybeFlush(uint64_t nowMs) {
    if (!m_fileLogging) return;
    if (nowMs - m_lastFlushMs >= FLUSH_INTERVAL_MS)
        flush();
}

void AppLogger::flush() {
    if (!m_fileLogging || m_pendingLines.empty()) return;

    // Attempt to obtain advisory lock on companion lockfile.  This prevents
    // interleaved writes when multiple processes share the same log path.
    int lockfd = -1;
    if (!m_logFilePath.empty()) {
        std::string lockpath = m_logFilePath + ".lock";
        lockfd = open(lockpath.c_str(), O_CREAT | O_RDWR, 0666);
        if (lockfd >= 0) {
            flock(lockfd, LOCK_EX);
        }
    }

    for (const auto& line : m_pendingLines)
        m_logFile << line << '\n';
    m_logFile.flush();
    m_pendingLines.clear();
    m_lastFlushMs = static_cast<uint64_t>(SDL_GetTicks());

    if (lockfd >= 0) {
        flock(lockfd, LOCK_UN);
        close(lockfd);
    }
}
