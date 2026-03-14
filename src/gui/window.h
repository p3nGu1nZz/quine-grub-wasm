#pragma once

#include "app.h"
#include "gui/heatmap.h"

#include <SDL3/SDL.h>

// Forward-declare ImGui types to avoid pulling in imgui.h in the header
struct ImFont;

// ── Gui ───────────────────────────────────────────────────────────────────────
//
// Owns the Dear ImGui SDL3/Renderer backend lifecycle and orchestrates all
// per-frame rendering.  Created once after the SDL3 window + renderer are up.
//
//   gui.init(window, renderer);
//   while (running) { gui.renderFrame(app); }
//   gui.shutdown();
// ─────────────────────────────────────────────────────────────────────────────

// Active GUI scene.
enum class GuiScene {
    TRAINING,   // startup training dashboard (shown before evolution)
    EVOLUTION,  // main simulation panels
};

class Gui {
public:
    // Initialize ImGui context and SDL3/Renderer backends.
    void init(SDL_Window* window, SDL_Renderer* renderer);

    // Process one full frame: NewFrame → render all panels → Present.
    void renderFrame(App& app);

    // Shut down ImGui backends and destroy the ImGui context.
    void shutdown();

private:
    SDL_Renderer* m_renderer  = nullptr;
    ImFont*       m_monoFont  = nullptr;

    // DPI scale computed during init; exposed for tests and potential layout
    // decisions.  Default is 1.0 for compatibility.
    float m_dpiScale = 1.0f;
    // Additional multiplier applied to UI elements (fonts, buttons) to ensure
    // touch-friendly, easy-to-read controls.  This is typically the DPI scale
    // boosted by a constant factor, with a minimum of 1.0.
    float m_uiScale  = 1.0f;

    // fps measurement helpers
    Uint64 m_prevCounter = 0;
    float  m_fps = 0.0f;
public:
    // Query the raw DPI-based scale.
    float dpiScale() const { return m_dpiScale; }
    // Query the effective UI scale used for fonts and widget dimensions.
    float uiScale() const { return m_uiScale; }
    // expose fps for tests
    float test_fps() const { return m_fps; }

private:
    // Current scene; starts at TRAINING
    GuiScene m_scene = GuiScene::TRAINING;

    // Sub-module: memory heatmap (owns its own heat-decay state)
    GuiHeatmap m_heatmap;

    // Cached textures for neural network weight heatmaps.  Building the
    // cell grid every frame was causing the evolution scene to drop to
    // single-digit FPS, so we instead rasterize the layer matrices into
    // SDL textures once per generation and just blit the resulting image
    // each frame.  The cache is invalidated when the app's generation
    // counter advances or the network layout changes.
    struct LayerTexture {
        SDL_Texture* tex = nullptr;
        int w = 0;
        int h = 0;
    };
    std::vector<LayerTexture> m_heatmapCache;
    int m_lastHeatmapGen = -1;

    // Per-frame scroll / auto-scroll state
    bool   m_scrollLogs   = true;
    bool   m_scrollInstrs = true;
    int    m_lastIP       = -1;
    size_t m_lastLogSz    = 0;

    // filter used in the log panel; matches substring in messages
    std::string m_logFilter;

    // advisor panel state
    bool   m_showAdvisor    = false;
    std::string m_lastDumpPath;

    // ── Panel helpers (each renders one independent UI region) ────────────────
    void renderTrainingScene(App& app, int winW, int winH);
    void renderTopBar(App& app, int winW);
    void renderLogPanel(const App& app, float w, float h);
    void renderInstrPanel(const App& app, float w, float h);
    void renderKernelPanel(const App& app, float w, float h);
    void renderAdvisorPanel(const App& app, float w, float h);
    void renderInstancesPanel(App& app, float w, float h);
    void renderStatusBar(const App& app);

    // draw heatmaps of each policy layer's weight matrix (used in evolution
    // scene).  Panels are stacked vertically and sized to fit the window width.
    void renderWeightHeatmaps(const App& app, int winW);


public:
    // helpers used by unit tests to probe cache state
    int test_heatmapCacheSize() const { return static_cast<int>(m_heatmapCache.size()); }
    SDL_Texture* test_heatmapTex(int idx) const {
        if (idx < 0 || idx >= (int)m_heatmapCache.size()) return nullptr;
        return m_heatmapCache[idx].tex;
    }
    int test_lastHeatmapGen() const { return m_lastHeatmapGen; }

    // expose the current scene enum for GUI tests
    int test_scene() const { return (int)m_scene; }

    // allow tests to directly exercise the internal heatmap renderer without
    // having to go through a full frame.  This simply forwards to the private
    // implementation.
    void test_renderWeightHeatmaps(const App& app, int winW) { renderWeightHeatmaps(app, winW); }
};
