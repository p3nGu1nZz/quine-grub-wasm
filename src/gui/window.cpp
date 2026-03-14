#include "gui/window.h"
#include "gui/colors.h"
#include "util.h"
#include "wasm/parser.h"

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <vector>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cstring>

// ─── Gui lifecycle ────────────────────────────────────────────────────────────

void Gui::init(SDL_Window* window, SDL_Renderer* renderer) {
    m_renderer = renderer;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Apply DPI scaling before loading fonts so the chosen font size is scaled
    // appropriately.  We also expose the raw DPI scale and the final UI scale
    // via members for testing and layout decisions.
    m_dpiScale = computeDpiScale(window);
    // Boost the raw scale slightly to make fonts/buttons large enough for
    // touch screens and maintain the sci-fi look, but avoid runaway sizes
    // on very large monitors.  Empirically the previous boost was too
    // aggressive on Linux, so we scale it back by about 35% (="UI_BOOST"
    // now ≈0.8125).  The final value is still clamped to [1.0, 2.0].
    const float UI_BOOST = 1.25f * 0.65f; // ~0.8125
    m_uiScale = std::max(1.0f, m_dpiScale * UI_BOOST);
    if (m_uiScale > 2.0f) m_uiScale = 2.0f;
    io.FontGlobalScale = m_uiScale;

    // Customize styling for a darker, neon-accented 'cyberpunk' look.
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FramePadding = { 8.0f * m_uiScale, 6.0f * m_uiScale };
    style.ItemSpacing  = { 8.0f * m_uiScale, 6.0f * m_uiScale };
    style.WindowBorderSize  = 0.0f;
    style.FrameBorderSize   = 1.0f;
    style.WindowRounding    = 0.0f;
    style.FrameRounding     = 2.0f;
    style.ScrollbarRounding = 0.0f;
    style.Colors[ImGuiCol_Button]          = {0.15f, 0.00f, 0.25f, 1.0f};
    style.Colors[ImGuiCol_ButtonHovered]   = {0.45f, 0.10f, 0.80f, 1.0f};
    style.Colors[ImGuiCol_ButtonActive]    = {0.60f, 0.20f, 1.00f, 1.0f};
    style.Colors[ImGuiCol_Header]          = {0.00f, 0.40f, 0.90f, 0.8f};
    style.Colors[ImGuiCol_HeaderHovered]   = {0.00f, 0.55f, 1.00f, 0.8f};
    style.Colors[ImGuiCol_HeaderActive]    = {0.00f, 0.70f, 1.00f, 0.8f};
    style.Colors[ImGuiCol_Text]            = {0.80f, 0.80f, 0.85f, 1.0f};
    style.Colors[ImGuiCol_WindowBg]        = {0.02f, 0.02f, 0.05f, 1.0f};

    // Load a monospace font; fall back to ImGui default if none found
    const char* fontPaths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
        nullptr
    };
    m_monoFont = nullptr;
    for (int i = 0; fontPaths[i]; i++) {
        m_monoFont = io.Fonts->AddFontFromFileTTF(fontPaths[i], 13.0f * m_uiScale);
        if (m_monoFont) break;
    }
    if (!m_monoFont)
        m_monoFont = io.Fonts->AddFontDefault();

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
}

void Gui::shutdown() {
    // release any cached textures we allocated for the weight heatmaps
    for (auto &c : m_heatmapCache) {
        if (c.tex) {
            SDL_DestroyTexture(c.tex);
            c.tex = nullptr;
        }
    }
    m_heatmapCache.clear();

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

// ─── Frame ───────────────────────────────────────────────────────────────────

void Gui::renderFrame(App& app) {
    // compute FPS
    Uint64 now = SDL_GetPerformanceCounter();
    if (m_prevCounter != 0) {
        double dt = (double)(now - m_prevCounter) / SDL_GetPerformanceFrequency();
        if (dt > 0.0) {
            m_fps = (float)(1.0 / dt);
        }
    }
    m_prevCounter = now;

    // Sync auto-scroll flags (only relevant for EVOLUTION scene)
    if (app.programCounter() != m_lastIP) {
        m_scrollInstrs = true;
        m_lastIP       = app.programCounter();
    }
    if (app.logs().size() != m_lastLogSz) {
        m_scrollLogs = true;
        m_lastLogSz  = app.logs().size();
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    int winW, winH;
    SDL_GetWindowSize(SDL_GetRenderWindow(m_renderer), &winW, &winH);

    if (m_scene == GuiScene::TRAINING) {
        renderTrainingScene(app, winW, winH);
    } else {
        // automatically switch to training if evolution has been disabled
        if (!app.evolutionEnabled() && app.trainingPhase() != TrainingPhase::COMPLETE) {
            m_scene = GuiScene::TRAINING;
        }
        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize({ (float)winW, (float)winH });
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::Begin("##Root", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoMove);

        if (m_monoFont) ImGui::PushFont(m_monoFont);

        renderTopBar(app, winW);

        float headerH = ImGui::GetCursorPosY();
        float footerH = 130.0f * m_uiScale;
        float panelH  = (float)winH - headerH - footerH;
        float logW    = (float)winW * 0.40f;
        float instrW  = 240.0f * m_uiScale;
        float kernelW = (float)winW - logW - instrW;

        renderLogPanel(app, logW, panelH);
        ImGui::SameLine();
        renderInstrPanel(app, instrW, panelH);
        ImGui::SameLine();
        renderKernelPanel(app, kernelW, panelH);

        if (m_showAdvisor) {
            ImGui::SameLine();
            renderAdvisorPanel(app, 300.0f * m_uiScale, panelH);
        }

        if (app.instanceCount() > 0) {
            ImGui::SameLine();
            renderInstancesPanel(app, 260.0f * m_uiScale, panelH);
        }

        // weight heatmaps per layer appear just above the memory panel
        renderWeightHeatmaps(app, winW);

        m_heatmap.renderPanel(app, winW);
        renderStatusBar(app);

        if (m_monoFont) ImGui::PopFont();
        ImGui::End();
    }

    // Use a neutral background color
    ImVec4 bg = { 0.01f, 0.03f, 0.10f, 1.0f };
    SDL_SetRenderDrawColorFloat(m_renderer, bg.x, bg.y, bg.z, 1.0f);
    SDL_RenderClear(m_renderer);
    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), m_renderer);
    SDL_RenderPresent(m_renderer);
}
