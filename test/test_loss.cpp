#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;
#include "nn/loss.h"
#include "nn/advisor.h"

// ─── Basic generation signal ──────────────────────────────────────────────────

TEST_CASE("Loss computation returns negative generation", "[loss]") {
    TelemetryEntry e;
    e.generation = 0;
    REQUIRE(Loss::compute(e) == Approx(0.0f));
    e.generation = 5;
    REQUIRE(Loss::compute(e) == Approx(-5.0f));
}

TEST_CASE("Loss is monotonically lower for higher generations (no other signals)", "[loss]") {
    TelemetryEntry e;
    e.generation = 1;
    float l1 = Loss::compute(e);
    e.generation = 10;
    float l10 = Loss::compute(e);
    e.generation = 100;
    float l100 = Loss::compute(e);
    REQUIRE(l10 < l1);
    REQUIRE(l100 < l10);
}

// ─── Trap penalty ────────────────────────────────────────────────────────────

TEST_CASE("Loss adds trap penalty of +8 when trapCode is non-empty", "[loss][trap]") {
    TelemetryEntry e;
    e.generation = 5;
    float noTrap  = Loss::compute(e);      // -5.0
    e.trapCode    = "unreachable";
    float withTrap = Loss::compute(e);     // -5.0 + 8.0 = 3.0
    REQUIRE(withTrap == Approx(noTrap + 8.0f));
}

TEST_CASE("Loss trap penalty fires regardless of trap message content", "[loss][trap]") {
    TelemetryEntry e;
    e.generation = 0;
    float base = Loss::compute(e); // 0.0

    for (const char* msg : {"oops", "stack overflow", "a"}) {
        e.trapCode = msg;
        REQUIRE(Loss::compute(e) == Approx(base + 8.0f));
    }
    e.trapCode.clear();
    REQUIRE(Loss::compute(e) == Approx(base));
}

// ─── Drop-ratio penalty ───────────────────────────────────────────────────────

TEST_CASE("Loss penalises excessive DROP opcodes (>40%)", "[loss][drop]") {
    TelemetryEntry e;
    e.generation = 0;
    float baseLoss = Loss::compute(e); // 0.0

    // 50% drops — exceeds 0.40 threshold by 0.10, penalty = 0.10 * 5 = 0.5
    e.opcodeSequence = {0x1A, 0x1A, 0x1A, 0x1A, 0x1A,  // 5 drops
                        0x41, 0x41, 0x42, 0x20, 0x0B};  // 5 other
    float highDrop = Loss::compute(e);
    REQUIRE(highDrop > baseLoss); // penalty was applied

    // Under-threshold: <40% drops → no drop penalty
    e.opcodeSequence = {0x1A, 0x41, 0x41, 0x41, 0x41,
                        0x42, 0x20, 0x21, 0x0B, 0x01};  // 10% drops
    float lowDrop = Loss::compute(e);
    // lowDrop may still differ from baseLoss due to other penalties (diversity,
    // length), but it must be strictly less than highDrop.
    REQUIRE(lowDrop < highDrop);
}

// ─── Diversity penalty ────────────────────────────────────────────────────────

TEST_CASE("Loss penalises low opcode diversity", "[loss][diversity]") {
    TelemetryEntry e;
    e.generation = 0;

    // Single unique opcode — maximum diversity penalty
    e.opcodeSequence.assign(64, 0x41); // all i32.const
    float lowDiv = Loss::compute(e);

    // Many unique opcodes across a richer sequence
    e.opcodeSequence.clear();
    for (int op = 0; op < 64; ++op)
        e.opcodeSequence.push_back((uint8_t)op); // 64 distinct opcodes
    float highDiv = Loss::compute(e);

    REQUIRE(highDiv < lowDiv); // more diverse = lower loss
}

// ─── Length penalty / reward ──────────────────────────────────────────────────

TEST_CASE("Loss penalises very short instruction sequences", "[loss][length]") {
    TelemetryEntry e;
    e.generation = 0;

    e.opcodeSequence = {0x0B};          // length 1 → penalty = (8-1)*0.5 = 3.5
    float shortLoss = Loss::compute(e);

    e.opcodeSequence.assign(8, 0x01);  // exactly at threshold (8 ops)
    float atThreshold = Loss::compute(e);

    // Short sequences must be penalised more than at-threshold ones
    REQUIRE(shortLoss > atThreshold);
}

TEST_CASE("Loss gives mild reward for long, rich sequences", "[loss][length]") {
    TelemetryEntry e;
    e.generation = 0;

    // Baseline: exactly 32 ops (no reward, no penalty)
    for (int i = 0; i < 32; ++i) e.opcodeSequence.push_back((uint8_t)(i % 64));
    float at32 = Loss::compute(e);

    // Longer: 128 ops with the same diversity profile
    e.opcodeSequence.clear();
    for (int i = 0; i < 128; ++i) e.opcodeSequence.push_back((uint8_t)(i % 64));
    float at128 = Loss::compute(e);

    // Longer should have equal or lower loss
    REQUIRE(at128 <= at32);
}

// ─── Combined signals ─────────────────────────────────────────────────────────

TEST_CASE("Loss combines all penalties: trap + drop + diversity + length", "[loss][combined]") {
    TelemetryEntry noisy;
    noisy.generation   = 3;
    noisy.trapCode     = "trap";           // +8.0
    noisy.opcodeSequence.assign(4, 0x1A); // 4 drops → >40 %, short → extra penalty

    TelemetryEntry clean;
    clean.generation   = 3;
    // empty sequence: no sequence penalties
    REQUIRE(Loss::compute(noisy) > Loss::compute(clean));
}
