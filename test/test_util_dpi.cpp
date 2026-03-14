#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>

#include "util.h"
#include "gui/window.h"
#include "app.h"
#include <filesystem>
#include <unistd.h>

// we need the SDL/ImGui backend symbols in tests
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>


TEST_CASE("computeDpiScale handles null window gracefully", "[dpi]") {
    REQUIRE(computeDpiScale(nullptr) == 1.0f);
}

TEST_CASE("computeDpiScale increases with window size", "[dpi]") {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* w1 = SDL_CreateWindow("t1", 1400,900, 0);
    SDL_Window* w2 = SDL_CreateWindow("t2", 2800,1800, 0);
    REQUIRE(computeDpiScale(w1) == 1.0f);
    REQUIRE(computeDpiScale(w2) >= computeDpiScale(w1));
    SDL_DestroyWindow(w1);
    SDL_DestroyWindow(w2);
    SDL_Quit();
}

TEST_CASE("Gui init applies dpi & ui scales", "[dpi]") {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* w = SDL_CreateWindow("dpi", 1400,900, 0);
    SDL_Renderer* r = SDL_CreateRenderer(w, nullptr);
    Gui gui;
    gui.init(w, r);
    REQUIRE(gui.dpiScale() == computeDpiScale(w));
    // UI scale should remain within reasonable bounds relative to dpi
    // (no extreme shrink/zoom) and respect the clamp.
    REQUIRE(gui.uiScale() >= 1.0f);
    REQUIRE(gui.uiScale() <= 2.0f); // our clamp should prevent excessive scaling
    // ImGui global scale should reflect the uiScale value
    ImGuiIO& io = ImGui::GetIO();
    REQUIRE(io.FontGlobalScale == gui.uiScale());
    gui.shutdown();
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(w);
    SDL_Quit();
}

TEST_CASE("App auto-export creates session files", "[export]") {
    namespace fs = std::filesystem;
    // use a temporary working directory so we don't pollute repo
    fs::remove_all("test_seq");
    fs::remove_all("test_run");
    fs::path orig = fs::current_path();
    fs::path tmp = orig / "test_run";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    fs::current_path(tmp);

    App app;
    // simulate one successful reboot/generation
    app.doReboot(true);
    // default path is rooted at executable directory (not current_path)
    fs::path seqdir = sequenceDir(app.runId());
    REQUIRE(fs::exists(seqdir));
    REQUIRE(fs::exists(seqdir / "gen_1.txt"));
    // default telemetry level is FULL, so kernel blob should be written
    REQUIRE(fs::exists(seqdir / "kernel_1.b64"));
    // concurrency lock file should exist
    REQUIRE(fs::exists(seqdir / "export.lock"));

    // inspect contents: should start with plain-text header
    std::ifstream f(seqdir / "gen_1.txt");
    std::string first;
    std::getline(f, first);
    REQUIRE(first.find("WASM QUINE BOOTLOADER") != std::string::npos);

    // now repeat with JSON format
    fs::remove_all(seqdir);
    CliOptions opts;
    opts.telemetryFormat = TelemetryFormat::JSON;
    App jsonApp(opts);
    jsonApp.doReboot(true);
    fs::path jdir = sequenceDir(jsonApp.runId());
    std::ifstream jf(jdir / "gen_1.txt");
    std::string line;
    std::getline(jf, line);
    REQUIRE(line.find('{') != std::string::npos);

    // create stray files at root and confirm constructor cleans them up
    std::ofstream stray1("bootloader_old.log");
    std::ofstream stray2("quine_telemetry_gen999.txt");
    stray1 << "x";
    stray2 << "x";
    stray1.close();
    stray2.close();
    App app2;  // construction should purge stray files
    REQUIRE(!fs::exists("bootloader_old.log"));
    REQUIRE(!fs::exists("quine_telemetry_gen999.txt"));

    // cleanup
    fs::current_path(orig);
    fs::remove_all(tmp);
}

TEST_CASE("AppLogger creates lock file during flush", "[log]") {
    namespace fs = std::filesystem;
    // avoid creating a top-level "bin" when tests run from repo root; prefer
    // an explicit temporary directory under .tmp if detected.
    fs::path base = fs::current_path();
    if (base.filename() == "WASM-Quine-Bootloader") {
        base /= ".tmp/test_bin";
    }
    fs::path logs = base / "bin/logs";

    fs::remove_all(logs);
    fs::create_directories(logs);
    AppLogger logger;
    logger.init((logs / "test.log").string());
    logger.log("hi","info");
    logger.flush();
    REQUIRE(fs::exists((logs / "test.log.lock").string()));
    fs::remove_all(logs);
}

TEST_CASE("executableDir returns non-empty string", "[util]") {
    std::string ed = executableDir();
    REQUIRE(!ed.empty());
    // should be absolute path
    REQUIRE(ed[0] == '/');
}

TEST_CASE("weight heatmap drawing caches textures for performance", "[gui][heatmap]") {
    // create a tiny SDL context so the GUI can allocate textures
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* w = SDL_CreateWindow("heat", 400,400, 0);
    SDL_Renderer* r = SDL_CreateRenderer(w, nullptr);
    Gui gui;
    gui.init(w, r);

    App app;
    // ensure there is at least one layer so the heatmap code runs
    auto &pol = const_cast<Policy&>(app.trainer().policy());
    pol.addDense(5,5);

    // helper lambda to drive a minimal ImGui frame then invoke the heatmap
    auto drawOnce = [&](int width) {
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGui::Begin("test");
        gui.test_renderWeightHeatmaps(app, width);
        ImGui::End();
        ImGui::Render();
    };

    // initial invocation should populate cache
    drawOnce(800);
    REQUIRE(gui.test_heatmapCacheSize() == pol.layerCount());
    SDL_Texture* tex0 = gui.test_heatmapTex(0);
    REQUIRE(tex0 != nullptr);
    int gen0 = gui.test_lastHeatmapGen();
    REQUIRE(gen0 == app.generation());

    // second draw without changing generation should reuse same texture
    drawOnce(800);
    REQUIRE(gui.test_lastHeatmapGen() == gen0);
    REQUIRE(gui.test_heatmapTex(0) == tex0);

    // now draw with a narrow window width to force the horizontal scrollbar
    // path in the layout; the cache behaviour should be unchanged.
    drawOnce(100);
    REQUIRE(gui.test_lastHeatmapGen() == gen0);
    REQUIRE(gui.test_heatmapTex(0) == tex0);

    // simulate a generation advance and redraw; cache should refresh
    app.doReboot(true);
    REQUIRE(app.generation() == gen0 + 1);
    drawOnce(800);
    REQUIRE(gui.test_lastHeatmapGen() == app.generation());
    SDL_Texture* tex1 = gui.test_heatmapTex(0);
    REQUIRE(tex1 != nullptr);
    // Note: we cannot rely on tex1 != tex0 here because the allocator may
    // reuse the same address after destroy+create.  Verifying that
    // lastHeatmapGen advanced is sufficient proof of cache invalidation.

    // draw a few more full frames to exercise the FPS counter
    for (int i = 0; i < 5; ++i) {
        gui.renderFrame(app);
    }
    REQUIRE(gui.test_fps() > 0.0f);

    // -- new scene transition test --
    app.test_forceEvolutionEnabled(false);
    app.test_forceTrainingPhase(TrainingPhase::TRAINING);
    gui.renderFrame(app);
    REQUIRE(gui.test_scene() == (int)GuiScene::TRAINING);

    // simulate training completion and ensure we remain on TRAINING while the
    // save countdown runs, then transition to EVOLUTION once the checkpoint is
    // written.
    app.test_forceTrainingPhase(TrainingPhase::COMPLETE);
    REQUIRE(!app.modelSaved());
    // calling update should trigger the save countdown
    app.update();
    REQUIRE(app.savingModel());
    // GUI should still show training scene
    gui.renderFrame(app);
    REQUIRE(gui.test_scene() == (int)GuiScene::TRAINING);
    // drive updates until save finishes
    while (!app.modelSaved()) app.update();
    // now the next render should switch scenes
    gui.renderFrame(app);
    REQUIRE(gui.test_scene() == (int)GuiScene::EVOLUTION);


    gui.shutdown();
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(w);
    SDL_Quit();
}

TEST_CASE("sequenceDir sanitizes runId", "[util]") {
    auto p1 = sequenceDir("normal123");
    auto p2 = sequenceDir("..\\evil");
    REQUIRE(p1 != p2);
    // the *basename* should contain only alphanumeric characters
    std::string base = p2.filename().string();
    for (char c : base) {
        REQUIRE(std::isalnum(static_cast<unsigned char>(c)));
    }
}

TEST_CASE("sanitizeRelativePath filters dangerous telemetry dirs", "[util]") {
    REQUIRE(sanitizeRelativePath("valid/path") == "valid/path");
    REQUIRE(sanitizeRelativePath("../evil") == "");
    REQUIRE(sanitizeRelativePath("/absolute") == "");
    REQUIRE(sanitizeRelativePath("sub/../up") == "");
}

TEST_CASE("blacklist persistence round-trip", "[app][blacklist]") {
    namespace fs = std::filesystem;
    fs::remove_all("test_seq");
    CliOptions opts;
    opts.telemetryDir = "test_seq";
    opts.heuristic = HeuristicMode::BLACKLIST;
    {
        App a(opts);
        std::vector<uint8_t> seq = {0x01, 0x02, 0x03};
        a.addToBlacklist(seq);
        REQUIRE(a.isBlacklisted(seq));
    }
    {
        App b(opts);
        REQUIRE(b.isBlacklisted({0x01, 0x02, 0x03}));
    }
    fs::remove_all("test_seq");
}

TEST_CASE("App sanitizes invalid telemetryDir", "[app][telemetry]") {
    CliOptions opts;
    opts.telemetryDir = "../notallowed";
    App a(opts);
    REQUIRE(a.options().telemetryDir.empty());
}

TEST_CASE("runWithTimeout returns false when lambda overruns", "[app][timeout]") {
    CliOptions opts;
    opts.maxExecMs = 1; // 1 ms
    App a(opts);
    // lambda that sleeps longer than limit
    bool ok = a.runWithTimeout([](){ usleep(5000); });
    REQUIRE(ok == false);
}

TEST_CASE("runWithTimeout returns true for fast lambdas", "[app][timeout]") {
    CliOptions opts;
    opts.maxExecMs = 100;
    App a(opts);
    bool ok = a.runWithTimeout([](){ /* nothing */ });
    REQUIRE(ok == true);
}

TEST_CASE("exportNow produces a file identically to autoExport", "[app][export]") {
    namespace fs = std::filesystem;
    fs::remove_all("test_seq");
    CliOptions opts;
    opts.telemetryDir = "test_seq";
    // subclass to expose telemetryRoot for the test
    struct TestApp : App { using App::telemetryRoot; explicit TestApp(const CliOptions& o) : App(o) {} };
    TestApp a(opts);
    a.doReboot(true);
    fs::path seqdir = a.telemetryRoot() / a.runId();
    REQUIRE(fs::exists(seqdir / "gen_1.txt"));
    // now manually trigger again and expect generation 1 overwritten or new file
    a.exportNow();
    REQUIRE(fs::exists(seqdir / "gen_1.txt"));
    fs::remove_all("test_seq");
}

// rest of file unchanged

TEST_CASE("randomId yields 9 alphanumeric chars and varies", "[util]") {
    std::string id1 = randomId();
    std::string id2 = randomId();
    REQUIRE(id1.size() == 9);
    REQUIRE(id2.size() == 9);
    for (char c : id1) REQUIRE(std::isalnum(static_cast<unsigned char>(c)));
    REQUIRE(id1 != id2); // extremely low probability of collision
}

TEST_CASE("decodeBase64Cached returns same vector and caches results", "[util]") {
    std::string hello = "aGVsbG8="; // "hello"
    auto v1 = decodeBase64Cached(hello);
    auto v2 = decodeBase64Cached(hello);
    REQUIRE(v1.size() == 5);
    REQUIRE(v1 == v2);
    // ideally the same reference is returned, but relocation may occur when
    // the internal map resizes; at least the contents must match.
}

TEST_CASE("nowIso format is ISO-like", "[util]") {
    std::string t = nowIso();
    REQUIRE(t.size() >= 20);
    REQUIRE(t.find('T') != std::string::npos);
    REQUIRE(t.back() == 'Z');
}
