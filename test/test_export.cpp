#include <catch2/catch_test_macros.hpp>
#include "exporter.h"

TEST_CASE("buildReport includes telemetry metrics when provided") {
    ExportData d;
    d.generation = 42;
    d.currentKernel = "AA==";
    d.instructions = {};
    d.logs = {};
    d.history = {};
    d.mutationsAttempted = 2;
    d.mutationsApplied = 1;
    d.mutationInsert = 1;
    d.mutationDelete = 0;
    d.mutationModify = 0;
    d.mutationAdd = 0;
    d.trapCode = "unreachable";
    d.genDurationMs = 123.4;
    d.kernelSizeMin = 10;
    d.kernelSizeMax = 20;
    d.heuristicBlacklistCount = 5;
    d.advisorEntryCount = 3;
    d.instances = {"AAA","BBB"};

    std::string report = buildReport(d);
    REQUIRE(report.find("Mutations Attempted: 2") != std::string::npos);
    REQUIRE(report.find("INSTANCES:") != std::string::npos);
    REQUIRE(report.find("AAA") != std::string::npos);
    REQUIRE(report.find("Mutations Applied: 1") != std::string::npos);
    REQUIRE(report.find("Mutation Breakdown: insert=1") != std::string::npos);
    REQUIRE(report.find("Traps: unreachable") != std::string::npos);
    REQUIRE(report.find("Gen Duration: 123.4 ms") != std::string::npos);
    REQUIRE(report.find("Kernel Size Min/Max: 10/20") != std::string::npos);
    REQUIRE(report.find("Heuristic Blacklist Entries: 5") != std::string::npos);
    REQUIRE(report.find("Advisor Entries: 3") != std::string::npos);
    // new field should be present even if sequence empty
    REQUIRE(report.find("OPCODE SEQUENCE:") != std::string::npos);
}
