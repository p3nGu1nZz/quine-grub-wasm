#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>

#include "util.h"
#include "types.h"
#include "fsm.h"
#include "base64.h"

#include <regex>
#include <set>

// ─── stateStr ────────────────────────────────────────────────────────────────

TEST_CASE("stateStr returns correct string for every SystemState", "[util][stateStr]") {
    REQUIRE(stateStr(SystemState::IDLE)            == "IDLE");
    REQUIRE(stateStr(SystemState::BOOTING)         == "BOOTING");
    REQUIRE(stateStr(SystemState::LOADING_KERNEL)  == "LOADING_KERNEL");
    REQUIRE(stateStr(SystemState::EXECUTING)       == "EXECUTING");
    REQUIRE(stateStr(SystemState::VERIFYING_QUINE) == "VERIFYING_QUINE");
    REQUIRE(stateStr(SystemState::SYSTEM_HALT)     == "SYSTEM_HALT");
    REQUIRE(stateStr(SystemState::REPAIRING)       == "REPAIRING");
}

// ─── randomId ────────────────────────────────────────────────────────────────

TEST_CASE("randomId produces 9-character alphanumeric strings", "[util][randomId]") {
    for (int i = 0; i < 20; ++i) {
        auto id = randomId();
        REQUIRE(id.size() == 9);
        for (char c : id)
            REQUIRE(std::isalnum(static_cast<unsigned char>(c)));
    }
}

TEST_CASE("randomId outputs are not all identical", "[util][randomId]") {
    std::set<std::string> ids;
    for (int i = 0; i < 30; ++i)
        ids.insert(randomId());
    // Collision probability for 30 draws from a 36^9 space is negligible
    REQUIRE(ids.size() > 1);
}

// ─── nowIso ──────────────────────────────────────────────────────────────────

TEST_CASE("nowIso returns a valid ISO-8601 UTC string", "[util][nowIso]") {
    auto ts = nowIso();
    // Expected: YYYY-MM-DDTHH:MM:SS.mmmZ
    std::regex iso(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z)");
    REQUIRE(std::regex_match(ts, iso));
}

// ─── nowFileStamp ─────────────────────────────────────────────────────────────

TEST_CASE("nowFileStamp returns YYYYMMDD_HHMMSS format", "[util][nowFileStamp]") {
    auto ts = nowFileStamp();
    REQUIRE(ts.size() == 15);
    std::regex stamp(R"(\d{8}_\d{6})");
    REQUIRE(std::regex_match(ts, stamp));
}

// ─── sanitizeRelativePath ────────────────────────────────────────────────────

TEST_CASE("sanitizeRelativePath accepts valid relative paths", "[util][sanitize]") {
    REQUIRE(sanitizeRelativePath("logs/run1") == "logs/run1");
    REQUIRE(sanitizeRelativePath("data")      == "data");
    REQUIRE(sanitizeRelativePath("a/b/c")     == "a/b/c");
}

TEST_CASE("sanitizeRelativePath rejects absolute paths", "[util][sanitize]") {
    REQUIRE(sanitizeRelativePath("/etc/passwd").empty());
    REQUIRE(sanitizeRelativePath("/tmp/logs").empty());
}

TEST_CASE("sanitizeRelativePath rejects path traversal", "[util][sanitize]") {
    REQUIRE(sanitizeRelativePath("../secret").empty());
    REQUIRE(sanitizeRelativePath("logs/../../etc").empty());
    REQUIRE(sanitizeRelativePath("a/../../b").empty());
}

TEST_CASE("sanitizeRelativePath returns empty on empty input", "[util][sanitize]") {
    REQUIRE(sanitizeRelativePath("").empty());
}

// ─── decodeBase64Cached ──────────────────────────────────────────────────────

TEST_CASE("decodeBase64Cached returns consistent results", "[util][cache]") {
    std::string b64 = "SGVsbG8="; // "Hello"
    const auto& first  = decodeBase64Cached(b64);
    const auto& second = decodeBase64Cached(b64);
    // Same reference means cached
    REQUIRE(&first == &second);
    REQUIRE(first.size() == 5);
    REQUIRE(first[0] == (uint8_t)'H');
    REQUIRE(first[4] == (uint8_t)'o');
}

// ─── BootFsm ─────────────────────────────────────────────────────────────────

TEST_CASE("BootFsm starts in IDLE state", "[fsm]") {
    BootFsm fsm;
    REQUIRE(fsm.current() == SystemState::IDLE);
}

TEST_CASE("BootFsm transition changes state and returns true", "[fsm]") {
    BootFsm fsm;
    bool changed = fsm.transition(SystemState::BOOTING);
    REQUIRE(changed);
    REQUIRE(fsm.current() == SystemState::BOOTING);
}

TEST_CASE("BootFsm transition no-op returns false on same state", "[fsm]") {
    BootFsm fsm;
    fsm.transition(SystemState::BOOTING);
    bool again = fsm.transition(SystemState::BOOTING);
    REQUIRE(!again);
    REQUIRE(fsm.current() == SystemState::BOOTING);
}

TEST_CASE("BootFsm transition callback fires exactly once per change", "[fsm]") {
    BootFsm fsm;
    int callCount = 0;
    SystemState fromSeen = SystemState::IDLE;
    SystemState toSeen   = SystemState::IDLE;
    fsm.setTransitionCallback([&](SystemState f, SystemState t) {
        ++callCount;
        fromSeen = f;
        toSeen   = t;
    });

    fsm.transition(SystemState::BOOTING);
    REQUIRE(callCount == 1);
    REQUIRE(fromSeen == SystemState::IDLE);
    REQUIRE(toSeen   == SystemState::BOOTING);

    // Same state: callback must NOT fire
    fsm.transition(SystemState::BOOTING);
    REQUIRE(callCount == 1);

    fsm.transition(SystemState::LOADING_KERNEL);
    REQUIRE(callCount == 2);
    REQUIRE(fromSeen == SystemState::BOOTING);
    REQUIRE(toSeen   == SystemState::LOADING_KERNEL);
}

TEST_CASE("BootFsm sequence of transitions", "[fsm]") {
    BootFsm fsm;
    std::vector<SystemState> visited;
    fsm.setTransitionCallback([&](SystemState, SystemState to) {
        visited.push_back(to);
    });

    fsm.transition(SystemState::BOOTING);
    fsm.transition(SystemState::LOADING_KERNEL);
    fsm.transition(SystemState::EXECUTING);
    fsm.transition(SystemState::VERIFYING_QUINE);
    fsm.transition(SystemState::IDLE);

    REQUIRE(visited.size() == 5);
    REQUIRE(visited[0] == SystemState::BOOTING);
    REQUIRE(visited[4] == SystemState::IDLE);
}

TEST_CASE("BootFsm enteredAt is non-decreasing across transitions", "[fsm]") {
    BootFsm fsm;
    uint64_t t0 = fsm.enteredAt(); // IDLE entry time
    fsm.transition(SystemState::BOOTING);
    uint64_t t1 = fsm.enteredAt();
    REQUIRE(t1 >= t0);
    fsm.transition(SystemState::EXECUTING);
    uint64_t t2 = fsm.enteredAt();
    REQUIRE(t2 >= t1);
}
