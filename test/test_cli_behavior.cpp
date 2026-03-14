#include <catch2/catch_test_macros.hpp>
#include "app.h"
#include "cli.h"
#include "util.h"  // for executableDir()
#include <filesystem>

TEST_CASE("App honors max-gen and stops updating", "[app][cli]") {
    CliOptions opts;
    opts.maxGen = 2;
    App a(opts);
    // simulate a couple of reboots using doReboot
    a.doReboot(true);
    REQUIRE(a.generation() == 1);
    REQUIRE(a.update() == true);
    a.doReboot(true);
    REQUIRE(a.generation() == 2);
    // next update should return false due to maxGen
    REQUIRE(a.update() == false);
}

TEST_CASE("autoExport respects telemetry-level", "[export][cli]") {
    namespace fs = std::filesystem;
    // remove any leftovers from previous runs so the test is hermetic
    struct TestApp : App { using App::telemetryRoot; explicit TestApp(const CliOptions& o) : App(o) {} };
    fs::path override = "test_seq";
    // compute root using telemetryRoot of a temporary app
    CliOptions tmpOpts;
    tmpOpts.telemetryDir = override;
    TestApp cleaner(tmpOpts);
    fs::remove_all(cleaner.telemetryRoot());

    CliOptions opts;
    opts.telemetryLevel = TelemetryLevel::BASIC;
    opts.telemetryDir = override;

    TestApp a(opts);
    a.doReboot(true);
    fs::path base = a.telemetryRoot() / a.runId();
    INFO("base=" << base);
    REQUIRE(fs::exists(base));
    std::ifstream fin((base / "gen_1.txt").string());
    REQUIRE(fin.good());
    std::string report((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
    REQUIRE(report.find("Final Generation") != std::string::npos);
    // BASIC level shouldn't include "CURRENT KERNEL"
    REQUIRE(report.find("CURRENT KERNEL") == std::string::npos);

    // if none, file is absent
    opts.telemetryLevel = TelemetryLevel::NONE;
    TestApp b(opts);
    b.doReboot(true);
    fs::path base2 = b.telemetryRoot() / b.runId();
    // remove everything under the override
    fs::remove_all(b.telemetryRoot());
    REQUIRE(!fs::exists(base2 / "gen_1.txt"));
}

TEST_CASE("profile flag outputs log entries", "[cli]") {
    CliOptions opts;
    opts.profile = true;
    App a(opts);
    // simulate two generations so profiling triggers
    a.doReboot(true);
    a.doReboot(true);
    // log buffer should contain at least one PROFILE line
    bool found = false;
    for (auto& e : a.logs()) {
        if (e.message.find("PROFILE") != std::string::npos) { found = true; break; }
    }
    REQUIRE(found);
}

TEST_CASE("App honors max-run-ms and stops updating", "[app][cli]") {
    uint64_t fakeTime = 0;
    auto nowFn = [&]() { return fakeTime; };
    CliOptions opts;
    opts.maxRunMs = 50;
    App a(opts, nowFn);
    // first call should succeed
    REQUIRE(a.update() == true);
    // advance past the limit and expect update to return false
    fakeTime += 100;
    REQUIRE(a.update() == false);
}

