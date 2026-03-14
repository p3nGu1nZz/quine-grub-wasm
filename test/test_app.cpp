#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;
#include <cmath>
#include <filesystem>
#include "util.h"  // for executableDir()
#include "app.h"
#include "constants.h"  // for KERNEL_SEQ

TEST_CASE("Blacklist helper methods behave correctly", "[app]") {
    CliOptions opts;
    opts.heuristic = HeuristicMode::BLACKLIST;
    App a(opts);
    std::vector<uint8_t> seq = {0x01, 0x02, 0x03};
    REQUIRE(!a.isBlacklisted(seq));
    a.addToBlacklist(seq);
    REQUIRE(a.isBlacklisted(seq));
    // adding again should not duplicate or change weight
    a.addToBlacklist(seq);
    REQUIRE(a.isBlacklisted(seq));
}

TEST_CASE("handleBootFailure records trap reason and blacklists mutation", "[app]") {
    CliOptions opts;
    opts.heuristic = HeuristicMode::BLACKLIST;
    App a(opts);
    std::vector<uint8_t> seq = {0x10, 0x20};
    a.test_simulateFailure("trap XYZ", seq);
    REQUIRE(a.lastTrapReason() == "trap XYZ");
    REQUIRE(a.isBlacklisted(seq));
}

TEST_CASE("exportHistory includes trap reason after failure", "[export]") {
    CliOptions opts;
    opts.heuristic = HeuristicMode::BLACKLIST;
    App a(opts);
    a.test_simulateFailure("oops", {0x55});
    std::string report = a.exportHistory();
    REQUIRE(report.find("Traps: oops") != std::string::npos);
}

TEST_CASE("heuristic decay reduces blacklist over generations", "[app][heuristic]") {
    CliOptions opts;
    opts.heuristic = HeuristicMode::DECAY;
    App a(opts);
    std::vector<uint8_t> seq = {0xAA, 0xBB};
    a.addToBlacklist(seq);
    REQUIRE(a.isBlacklisted(seq));
    // three successful reboots should remove weight=3 entry
    a.doReboot(true);
    REQUIRE(a.isBlacklisted(seq));
    a.doReboot(true);
    REQUIRE(a.isBlacklisted(seq));
    a.doReboot(true);
    REQUIRE(!a.isBlacklisted(seq));
}

TEST_CASE("App loads trainer state from CLI option", "[app][trainer]") {
    // prepare a trainer state file
    Trainer t;
    TelemetryEntry e; e.generation = 1;
    t.observe(e);
    std::string tmp = "app_trainer.tmp";
    REQUIRE(t.save(tmp));

    CliOptions opts;
    opts.loadModelPath = tmp;
    App a(opts);
    // the app's trainer should have the same forward value as t
    auto orig = t.policy().forward(std::vector<float>(256,0));
    auto loaded = a.trainer().policy().forward(std::vector<float>(256,0));
    REQUIRE(loaded.size() == orig.size());
    // comparison must tolerate NaN (weight initialization may produce it)
    if (std::isnan(orig[0])) {
        REQUIRE(std::isnan(loaded[0]));
    } else {
        REQUIRE(loaded[0] == Approx(orig[0]));
    }

    std::remove(tmp.c_str());
}

TEST_CASE("App saves trainer state to CLI option", "[app][trainer]") {
    CliOptions opts;
    opts.saveModelPath = "trainer_save.tmp";
    App a(opts);
    // manually invoke the training/write logic that the app would execute
    TelemetryEntry te;
    te.generation = a.generation();
    te.kernelBase64 = a.currentKernel();
    te.trapCode = a.lastTrapReason();
    a.trainAndMaybeSave(te);
    REQUIRE(std::filesystem::exists("trainer_save.tmp"));
    Trainer t;
    REQUIRE(t.load("trainer_save.tmp"));
    std::remove("trainer_save.tmp");
}

TEST_CASE("spawnInstance records kernels and logs", "[app][spawn]") {
    CliOptions opts;
    App a(opts);
    REQUIRE(a.instanceCount() == 0);
    a.spawnInstance("AAA");
    REQUIRE(a.instanceCount() == 1);
    REQUIRE(a.instances()[0] == "AAA");
    a.spawnInstance("BBB");
    REQUIRE(a.instanceCount() == 2);
}

TEST_CASE("killInstance removes kernels and logs", "[app][spawn]") {
    CliOptions opts;
    App a(opts);
    a.spawnInstance("X");
    a.spawnInstance("Y");
    REQUIRE(a.instanceCount() == 2);
    a.killInstance(0);
    REQUIRE(a.instanceCount() == 1);
    REQUIRE(a.instances()[0] == "Y");
    // killing out-of-range should be safe and leave count unchanged
    a.killInstance(5);
    REQUIRE(a.instanceCount() == 1);
}

TEST_CASE("exportHistory includes instances section", "[export]") {
    CliOptions opts;
    App a(opts);
    a.spawnInstance("ONE");
    a.spawnInstance("TWO");
    std::string report = a.exportHistory();
    REQUIRE(report.find("INSTANCES:") != std::string::npos);
    REQUIRE(report.find("ONE") != std::string::npos);
    REQUIRE(report.find("TWO") != std::string::npos);
}

TEST_CASE("App.log helper adds entries", "[app][log]") {
    CliOptions opts;
    App a(opts);
    // The constructor may auto-load a model checkpoint and add a log
    // entry; record the baseline size rather than assuming zero.
    size_t baseline = a.logs().size();
    a.log("test message", "debug");
    REQUIRE(a.logs().size() == baseline + 1);
    REQUIRE(a.logs().back().message == "test message");
}

TEST_CASE("App respects CLI kernel selection", "[app][cli]") {
    CliOptions opts;
    opts.kernelType = KernelType::SEQ;
    App a(opts);
    REQUIRE(a.currentKernel() == KERNEL_SEQ);
    REQUIRE(a.stableKernel() == KERNEL_SEQ);
}

TEST_CASE("App.scoreSequence delegates to Advisor and returns reasonable values", "[app][advisor]") {
    // use a fresh, empty telemetry directory to avoid leftover data from
    // earlier test runs.  Advisor scans this path on construction so we
    // remove it first to guarantee emptiness.
    namespace fs = std::filesystem;
    std::string tmpdir = "score_seq_test";
    fs::remove_all(tmpdir);

    CliOptions opts;
    opts.telemetryDir = tmpdir;
    App a(opts);
    std::vector<uint8_t> seq = {0xDE, 0xAD, 0xBE, 0xEF};

    // create a second Advisor pointing at the same base and compare results
    Advisor adv(a.seqBaseDir().string());
    float direct = adv.score(seq);
    REQUIRE(a.scoreSequence(seq) == Approx(direct));
}

TEST_CASE("blacklist persists across App instances", "[app][blacklist][persistence]") {
    namespace fs = std::filesystem;
    CliOptions opts;
    opts.telemetryDir = "bltest";
    opts.heuristic = HeuristicMode::BLACKLIST;
    struct TestApp : App { using App::telemetryRoot; explicit TestApp(const CliOptions& o) : App(o) {} };
    TestApp cleaning(opts);
    fs::remove_all(cleaning.telemetryRoot());

    {
        TestApp a(opts);
        std::vector<uint8_t> seq{0x1,0x2};
        REQUIRE(!a.isBlacklisted(seq));
        a.addToBlacklist(seq);
        REQUIRE(a.isBlacklisted(seq));
    }
    TestApp b(opts);
    std::vector<uint8_t> seq{0x1,0x2};
    REQUIRE(b.isBlacklisted(seq));
    fs::remove_all(b.telemetryRoot());
}

TEST_CASE("telemetryRoot derives from exe directory and respects override", "[app][telemetry]") {
    struct TestApp : App {
        using App::telemetryRoot;
        explicit TestApp(const CliOptions& o) : App(o) {}
    };
    CliOptions opts;
    TestApp a(opts);
    auto root = a.telemetryRoot();
    // should live under bin/seq and not contain "/test/"
    std::string s = root.string();
    REQUIRE(s.find("bin/seq") != std::string::npos);
    REQUIRE(s.find("/test/") == std::string::npos);

    opts.telemetryDir = "xyz";
    TestApp b(opts);
    auto root2 = b.telemetryRoot();
    REQUIRE(root2.string().find("xyz") != std::string::npos);
}

TEST_CASE("App constructor avoids bin/bin nested paths", "[app][telemetry]") {
    CliOptions opts;
    App a(opts);
    auto logs = a.logsDir();
    auto seq  = a.seqBaseDir();
    REQUIRE(logs.string().find("/bin/bin/") == std::string::npos);
    REQUIRE(seq.string().find("/bin/bin/") == std::string::npos);
}

TEST_CASE("App.requestExit triggers update to return false", "[app][signal]") {
    CliOptions opts;
    App a(opts);
    // update once to initialise clocks
    REQUIRE(a.update());
    // request exit and ensure subsequent update() returns false
    a.requestExit();
    REQUIRE(a.update() == false);
}

TEST_CASE("global requestAppExit helper sets flag and stops app", "[app][signal]") {
    CliOptions opts;
    App a(opts);
    REQUIRE(a.update());
    // call the externally-visible helper which would normally run from a
    // signal handler
    requestAppExit();
    REQUIRE(a.update() == false);
}

TEST_CASE("evolution auto-stops at configured generation and switches to training", "[app][evolution]") {
    CliOptions opts;
    App a(opts);
    a.enableEvolution();
    REQUIRE(a.evolutionEnabled());
    for (int i = 0; i < 49; ++i) {
        a.doReboot(true);
    }
    REQUIRE(a.evolutionEnabled());
    a.doReboot(true);
    REQUIRE(!a.evolutionEnabled());
    REQUIRE(a.trainingPhase() != TrainingPhase::COMPLETE);
}

TEST_CASE("update() re-enables evolution when training has already completed", "[app][evolution]") {
    CliOptions opts;
    App a(opts);
    // pretend training already finished but evolution disabled
    a.test_forceTrainingPhase(TrainingPhase::COMPLETE);
    a.test_forceModelSaved(true);
    a.test_forceEvolutionEnabled(false);
    REQUIRE_FALSE(a.evolutionEnabled());
    a.update();
    REQUIRE(a.evolutionEnabled());
}

TEST_CASE("evolution/training cycles can repeat indefinitely", "[app][cycle]") {
    CliOptions opts;
    App a(opts);
    a.enableEvolution();
    // drive the first auto-train trigger
    for (int i = 0; i < kAutoTrainGen; ++i)
        a.doReboot(true);
    REQUIRE(!a.evolutionEnabled());

    // simulate finishing the training phase and let update re-enable
    a.test_forceTrainingPhase(TrainingPhase::COMPLETE);
    a.test_forceModelSaved(true);
    a.update();
    REQUIRE(a.evolutionEnabled());

    // now drive a second auto-train threshold; generation has increased
    for (int i = 0; i < kAutoTrainGen; ++i)
        a.doReboot(true);
    REQUIRE(!a.evolutionEnabled());
}

