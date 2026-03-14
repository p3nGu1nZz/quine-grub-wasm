#include <catch2/catch_test_macros.hpp>
#include "log.h"
#include "types.h"

// ─── AppLogger in-memory operations ──────────────────────────────────────────
//
// These tests exercise only the pure in-memory paths (logs deque, history
// vector).  File-logging and SDL_GetTicks-dependent flush paths are covered
// by the flush/init path in production.

TEST_CASE("AppLogger starts with empty logs and history", "[log]") {
    AppLogger logger;
    REQUIRE(logger.logs().empty());
    REQUIRE(logger.history().empty());
}

TEST_CASE("AppLogger::log adds an entry to the ring buffer", "[log]") {
    AppLogger logger;
    logger.log("hello", "info");
    REQUIRE(logger.logs().size() == 1);
    REQUIRE(logger.logs().back().message == "hello");
    REQUIRE(logger.logs().back().type    == "info");
}

TEST_CASE("AppLogger::log defaults type to 'info'", "[log]") {
    AppLogger logger;
    logger.log("default type");
    REQUIRE(logger.logs().back().type == "info");
}

TEST_CASE("AppLogger::log records the id field as a non-empty string", "[log]") {
    AppLogger logger;
    logger.log("test");
    REQUIRE(!logger.logs().back().id.empty());
}

TEST_CASE("AppLogger accumulates multiple entries", "[log]") {
    AppLogger logger;
    const char* types[] = {"info", "success", "warning", "error", "system", "mutation"};
    for (const char* t : types)
        logger.log(std::string("msg:") + t, t);
    REQUIRE(logger.logs().size() == 6);
}

TEST_CASE("AppLogger caps ring buffer at MAX_LOG_ENTRIES", "[log][cap]") {
    AppLogger logger;
    // Push more entries than the cap; use different messages to avoid dedup
    for (size_t i = 0; i <= AppLogger::MAX_LOG_ENTRIES + 50; ++i) {
        logger.log("line " + std::to_string(i), "info");
    }
    // The deque must never exceed the cap
    REQUIRE(logger.logs().size() <= AppLogger::MAX_LOG_ENTRIES);
}

TEST_CASE("AppLogger::addHistory appends to the history ledger", "[log][history]") {
    AppLogger logger;
    HistoryEntry he;
    he.generation = 7;
    he.timestamp  = "2026-01-01T00:00:00.000Z";
    he.size       = 128;
    he.action     = "REBOOT";
    he.details    = "success";
    he.success    = true;
    logger.addHistory(he);

    REQUIRE(logger.history().size() == 1);
    REQUIRE(logger.history()[0].generation == 7);
    REQUIRE(logger.history()[0].action     == "REBOOT");
    REQUIRE(logger.history()[0].success    == true);
}

TEST_CASE("AppLogger history is unbounded and preserves order", "[log][history]") {
    AppLogger logger;
    for (int i = 0; i < 10; ++i) {
        HistoryEntry e;
        e.generation = i;
        e.success    = (i % 2 == 0);
        logger.addHistory(e);
    }
    REQUIRE(logger.history().size() == 10);
    for (int i = 0; i < 10; ++i)
        REQUIRE(logger.history()[i].generation == i);
}

TEST_CASE("AppLogger::log deduplicates identical messages within 100 ms", "[log][dedup]") {
    AppLogger logger;
    // Log the same message twice back-to-back (SDL_GetTicks returns the same
    // or very close value in the same thread iteration, so dedup fires).
    logger.log("duplicate me", "info");
    size_t after1 = logger.logs().size();
    logger.log("duplicate me", "info");
    size_t after2 = logger.logs().size();
    // Second identical message within the dedup window should be suppressed
    REQUIRE(after2 == after1);
}

TEST_CASE("AppLogger::log does not dedup different messages", "[log][dedup]") {
    AppLogger logger;
    logger.log("message A", "info");
    logger.log("message B", "info");
    REQUIRE(logger.logs().size() == 2);
    REQUIRE(logger.logs().front().message == "message A");
    REQUIRE(logger.logs().back().message  == "message B");
}

TEST_CASE("AppLogger::flush is a no-op without file logging enabled", "[log][flush]") {
    AppLogger logger;
    logger.log("before flush", "info");
    REQUIRE_NOTHROW(logger.flush()); // must not throw
    REQUIRE(logger.logs().size() == 1); // entries still in memory
}
