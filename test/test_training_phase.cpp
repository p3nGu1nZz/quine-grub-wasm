#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;
#include <iostream>
#include "app.h"
#include "nn/policy.h"
#include <filesystem>

// Helper to return a CliOptions with a fresh telemetry directory.  The
// directory is removed if it already exists, ensuring advisor starts empty.
static CliOptions freshOpts(bool useGui = true) {
    static int s_counter = 0;
    CliOptions opts;
    opts.useGui = useGui;
    opts.telemetryDir = "test_tp_" + std::to_string(s_counter++);
    struct TmpApp : App { using App::telemetryRoot; explicit TmpApp(const CliOptions& o) : App(o) {} };
    TmpApp tmp(opts);
    std::filesystem::remove_all(tmp.telemetryRoot());
    return opts;
}

// Helper: advance App by calling update() N times without a time source.
// Uses a CliOptions with useGui=true and an injected clock to avoid SDL.
static void tickN(App& app, int n) {
    for (int i = 0; i < n; ++i)
        app.update();
}

// ── Training phase: initial state ────────────────────────────────────────────

TEST_CASE("App GUI mode starts in LOADING training phase", "[training]") {
    CliOptions opts = freshOpts(true);
    App app(opts, []() -> uint64_t { return 0; });
    REQUIRE(app.trainingPhase() == TrainingPhase::LOADING);
    REQUIRE(!app.trainingDone());
    REQUIRE(!app.isPaused());
}

TEST_CASE("App headless mode bypasses training immediately", "[training]") {
    CliOptions opts = freshOpts(false);
    opts.useGui = false;
    App app(opts, []() -> uint64_t { return 0; });
    REQUIRE(app.trainingDone());
    REQUIRE(app.trainingPhase() == TrainingPhase::COMPLETE);
    REQUIRE(app.trainingProgress() == Approx(1.0f));
}

// ── Progress is monotonically increasing ─────────────────────────────────────

TEST_CASE("trainingProgress is monotonically increasing", "[training]") {
    CliOptions opts = freshOpts(true);
    uint64_t fakeTick = 0;
    App app(opts, [&fakeTick]() -> uint64_t { return fakeTick++; });

    float prev = app.trainingProgress();
    REQUIRE(prev >= 0.0f);
    REQUIRE(prev < 1.0f);

    for (int i = 0; i < 80; ++i) {
        app.update();
        float cur = app.trainingProgress();
        REQUIRE(cur >= prev);  // strictly non-decreasing
        prev = cur;
    }
}

// ── Phase transitions ─────────────────────────────────────────────────────────

TEST_CASE("Training phase transitions LOADING -> TRAINING -> COMPLETE", "[training]") {
    CliOptions opts = freshOpts(true);
    uint64_t fakeTick = 0;
    App app(opts, [&fakeTick]() -> uint64_t { return fakeTick++; });

    REQUIRE(app.trainingPhase() == TrainingPhase::LOADING);
    // sanity-check the load end value so we know why TRAINING may not start
    CAPTURE(app.test_trainingLoadEnd());
    int loadEnd = app.test_trainingLoadEnd();
    REQUIRE(loadEnd > 0);
    REQUIRE(loadEnd < 1000);  // should be small in tests

    // Drive until TRAINING phase (should happen within a reasonable
    // number of steps; fail loudly if it does not).
    int iters = 0;
    while (app.trainingPhase() == TrainingPhase::LOADING && ++iters < 1000) {
        app.update();
        CAPTURE(iters, app.test_trainingStep(), app.test_trainingLoadEnd());
        if (iters % 50 == 0) {
            WARN("iter=" << iters << " step=" << app.test_trainingStep()
                 << " loadEnd=" << app.test_trainingLoadEnd()
                 << " evo=" << app.evolutionEnabled()
                 << " phase=" << (int)app.trainingPhase());
        }
    }
    CAPTURE(iters, app.test_trainingStep());
    // debug: ensure we advanced at least to load end
    REQUIRE(app.test_trainingStep() >= app.test_trainingLoadEnd());
    // this final check will still run if above fails, giving phase info
    REQUIRE(app.trainingPhase() == TrainingPhase::TRAINING);

    // Now drive until COMPLETE; this loop will definitely terminate because
    // trainingTotal is finite.  it's safe to run without an artificial cap.
    while (app.trainingPhase() == TrainingPhase::TRAINING) {
        app.update();
    }
    REQUIRE(app.trainingPhase() == TrainingPhase::COMPLETE);
    REQUIRE(app.trainingDone());
    REQUIRE(app.trainingProgress() == Approx(1.0f));
}

// ── Progress reaches exactly 1.0 after COMPLETE ──────────────────────────────

TEST_CASE("trainingProgress returns 1.0 when training is complete", "[training]") {
    CliOptions opts = freshOpts(true);
    uint64_t fakeTick = 0;
    App app(opts, [&fakeTick]() -> uint64_t { return fakeTick++; });

    // Advance until training is reported complete (no hard-coded step count).
    while (!app.trainingDone()) {
        app.update();
    }
    REQUIRE(app.trainingProgress() == Approx(1.0f));
    REQUIRE(app.trainingDone());
}

// ── enableEvolution ───────────────────────────────────────────────────────────

TEST_CASE("enableEvolution immediately marks training complete and starts evolution", "[training]") {
    CliOptions opts = freshOpts(true);
    opts.useGui = true;
    uint64_t fakeTick = 0;
    App app(opts, [&fakeTick]() -> uint64_t { return fakeTick++; });

    REQUIRE(!app.trainingDone());

    // Call enableEvolution mid-training
    app.enableEvolution();

    REQUIRE(app.trainingDone());
    REQUIRE(app.trainingPhase() == TrainingPhase::COMPLETE);
    REQUIRE(app.trainingProgress() == Approx(1.0f));

    // Now update() should run the FSM (IDLE → startBoot) instead of training
    // Simply check that update() returns true (no crash/hang)
    REQUIRE(app.update());
}

// ── Model saving countdown ─────────────────────────────────────────────────

TEST_CASE("App saves model after training completion", "[training]") {
    CliOptions opts = freshOpts(true);
    uint64_t fakeTick = 0;
    App app(opts, [&fakeTick]() -> uint64_t { return fakeTick++; });

    // drive until training is done
    while (!app.trainingDone())
        app.update();
    REQUIRE(app.trainingDone());
    // the save countdown begins on the NEXT update after training completes
    app.update();
    REQUIRE(app.savingModel());
    REQUIRE(app.saveProgress() > 0.0f);

    int prevPhase = app.test_savePhase();
    app.update();
    REQUIRE(app.test_savePhase() >= prevPhase);
    float prog1 = app.saveProgress();
    REQUIRE(prog1 <= 1.0f);

    int iters = 0;
    while (!app.modelSaved() && ++iters < 10)
        app.update();
    REQUIRE(app.modelSaved());
    REQUIRE(!app.savingModel());
    REQUIRE(prog1 <= app.saveProgress());
    REQUIRE(app.saveProgress() == Approx(1.0f));
}

// ── Policy bounds safety ──────────────────────────────────────────────────────

TEST_CASE("Policy layer accessors are bounds-safe", "[policy]") {
    Policy p;
    p.addDense(4, 2);

    // Valid index
    REQUIRE(p.layerCount() == 1);
    REQUIRE(p.layerInSize(0) == 4);
    REQUIRE(p.layerOutSize(0) == 2);
    REQUIRE(p.layerWeights(0).size() == 8u);
    REQUIRE(p.layerBiases(0).size() == 2u);

    // Out-of-bounds: should return empty (no crash)
    REQUIRE(p.layerInSize(-1) == 0);
    REQUIRE(p.layerOutSize(99) == 0);
    REQUIRE(p.layerWeights(-1).empty());
    REQUIRE(p.layerBiases(99).empty());
}

// ── No uptime regression in headless ─────────────────────────────────────────

TEST_CASE("Headless App update() reaches the FSM (not stuck in training)", "[training]") {
    CliOptions opts = freshOpts(false);
    opts.useGui = false;
    App app(opts, []() -> uint64_t { return 0; });

    // Should be immediately past training; FSM should advance on update()
    REQUIRE(app.update());
    // State should have moved away from IDLE after one tick
    // (startBoot transitions IDLE -> BOOTING)
    REQUIRE(app.state() != SystemState::IDLE);
}
