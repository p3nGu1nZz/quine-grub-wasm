#include "gui/panels.h"
#include "gui/window.h"
#include "gui/colors.h"
#include "util.h"
#include "wasm/parser.h"

#include <cstdio>
#include <cmath>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <vector>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cstring>

// ─── Training scene ──────────────────────────────────────────────────────────

void Gui::renderTrainingScene(App& app, int winW, int winH) {
    ImGui::SetNextWindowPos({ 0, 0 });
    ImGui::SetNextWindowSize({ (float)winW, (float)winH });
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##TrainRoot", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoMove);

    if (m_monoFont) ImGui::PushFont(m_monoFont);

    // ── Header ───────────────────────────────────────────────────────────────
    float t = (float)ImGui::GetTime();
    float pulse = 0.7f + 0.3f * sinf(t * 2.0f);

    ImDrawList* hdl = ImGui::GetWindowDrawList();
    ImVec2 headerPos = ImGui::GetCursorScreenPos();
    float scanX = fmodf(t * 180.0f, (float)winW);
    hdl->AddLine({scanX, headerPos.y}, {scanX, headerPos.y + 2.0f},
                 IM_COL32(30, 210, 235, 80), 1.5f);

    ImGui::Separator();
    ImGui::TextColored({ 0.11f * pulse + 0.05f, 0.83f * pulse, 0.93f * pulse, 1.0f },
                       "quine-grub-wasm v2.4");
    ImGui::SameLine(0, 20);
    ImGui::TextColored({ 0.78f, 0.50f, 0.98f, 1.0f },
                       "NEURAL NETWORK RL TRAINING DASHBOARD");
    ImGui::SameLine(0, 20);
    // animated tick counter
    int tickSymbol = (int)(t * 4.0f) % 4;
    const char* spinners[] = { "|", "/", "-", "\\" };
    ImGui::TextColored({ 0.60f, 0.60f, 0.60f, 0.8f }, "%s", spinners[tickSymbol]);
    ImGui::Separator();

    // ── Central panel ────────────────────────────────────────────────────────
    float panelW = std::min((float)winW - 40.0f, 900.0f * m_uiScale);
    float panelX = ((float)winW - panelW) * 0.5f;
    ImGui::SetCursorPosX(panelX);

    ImGui::BeginChild("##TrainPanel",
        { panelW, (float)winH - (40.0f + 30.0f) * m_uiScale }, true,
        ImGuiWindowFlags_NoScrollbar);

    // Phase indicator
    const char* phaseStr = "LOADING TELEMETRY";
    ImVec4      phaseCol = { 0.39f, 0.66f, 0.97f, 1.0f };
    if (app.trainingPhase() == TrainingPhase::TRAINING) {
        phaseStr = "TRAINING POLICY";
        phaseCol = { 0.98f, 0.82f, 0.10f, 1.0f };
    } else if (app.trainingPhase() == TrainingPhase::COMPLETE) {
        phaseStr = "TRAINING COMPLETE";
        phaseCol = { 0.29f, 0.87f, 0.38f, 1.0f };
    }
    ImGui::TextColored(phaseCol, "PHASE: %s", phaseStr);
    ImGui::Spacing();

    // ── Side-by-side: telemetry stats | policy architecture ──────────────────
    float halfW = (panelW - 30.0f) * 0.5f;

    ImGui::BeginChild("##TelStats", { halfW, 130.0f * m_uiScale }, true);
    ImGui::TextDisabled("TELEMETRY DATA");
    ImGui::Separator();
    ImGui::Text("Entries loaded : %d", (int)app.advisor().entryCount());
    ImGui::Text("Observations   : %d", app.trainer().observations());
    if (app.advisor().entryCount() > 0) {
        float avgGen = 0.0f;
        for (const auto& e : app.advisor().entries())
            avgGen += static_cast<float>(e.generation);
        avgGen /= static_cast<float>(app.advisor().entryCount());
        ImGui::Text("Avg generation : %.1f", avgGen);
        float score = app.advisor().score({});
        ImGui::Text("Advisor score  : %.3f", score);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##PolicyArch", { halfW, 130.0f * m_uiScale }, true);
    ImGui::TextDisabled("POLICY NETWORK");
    ImGui::Separator();
    const Policy& pol = app.trainer().policy();
    int totalParams = 0;
    for (int l = 0; l < pol.layerCount(); ++l) {
        int w = pol.layerInSize(l) * pol.layerOutSize(l);
        int b = pol.layerOutSize(l);
        totalParams += w + b;
        ImGui::Text("Layer %d: %4d -> %-4d  (Dense)", l, pol.layerInSize(l), pol.layerOutSize(l));
    }
    ImGui::Separator();
    ImGui::Text("Total params   : %d", totalParams);
    if (app.trainer().observations() > 0)
        ImGui::Text("Avg loss (EMA) : %.6f", app.trainer().avgLoss());
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Separator();

    // ── Overall progress bar ──────────────────────────────────────────────────
    float prog = app.trainingProgress();
    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "%.0f%%  (%d obs)",
                  prog * 100.0f, app.trainer().observations());
    float glowA = 0.75f + 0.25f * sinf((float)ImGui::GetTime() * 3.0f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                          { 0.11f, 0.83f * glowA, 0.93f * glowA, 0.9f });
    ImGui::ProgressBar(prog, { -1.0f, 22.0f * m_uiScale }, overlay);
    ImGui::PopStyleColor();
    // animated scan line over bar
    {
        ImVec2 barMin = ImGui::GetItemRectMin();
        ImVec2 barMax = ImGui::GetItemRectMax();
        float barW = barMax.x - barMin.x;
        float scanPos = barMin.x + fmodf((float)ImGui::GetTime() * 120.0f, barW);
        ImGui::GetWindowDrawList()->AddLine(
            {scanPos, barMin.y}, {scanPos, barMax.y},
            IM_COL32(255, 255, 255, 60), 2.0f);
    }

    // ── Model-saving progress (visible when we are counting down to a
    // checkpoint write).  We deliberately keep the training scene active
    // while this is happening so the user sees the bar; evolution does not
    // resume until after the save completes.
    if (app.savingModel()) {
        float sprog = app.saveProgress();
        char o2[64];
        std::snprintf(o2, sizeof(o2), "Saving model... %.0f%%", sprog * 100.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, { 0.29f, 0.87f, 0.38f, 0.85f });
        ImGui::ProgressBar(sprog, { -1.0f, 22.0f * m_uiScale }, o2);
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Advisor entry table ───────────────────────────────────────────────────
    ImGui::TextDisabled("RECENT TELEMETRY ENTRIES");
    ImGui::Spacing();

    if (app.advisor().entryCount() == 0) {
        // show the actual telemetry root so users running from different
        // working directories know where data is being searched for.  the
        // path is derived from the executable location (see App::telemetryRoot()).
        std::string telemetry = app.telemetryRootPublic().string();
        if (!telemetry.empty()) {
            // add trailing slash for readability
            if (telemetry.back() != '/' && telemetry.back() != '\\')
                telemetry += '/';
            ImGui::TextDisabled("No prior run sequences found in %s", telemetry.c_str());
        } else {
            ImGui::TextDisabled("No prior run sequences found (telemetry root unknown)");
        }
        ImGui::TextDisabled("Training will begin fresh once evolution starts.");
    } else {
        // Table header
        ImGui::TextDisabled("%-6s  %-20s  %s", "GEN", "TRAP", "KERNEL (first 40 chars)");
        ImGui::Separator();
        ImGui::BeginChild("##EntryScroll", { 0, 160.0f * m_uiScale }, false,
                          ImGuiWindowFlags_HorizontalScrollbar);
        const auto& entries = app.advisor().entries();
        // show at most 50 most-recent entries
        int startIdx = (int)entries.size() > 50 ? (int)entries.size() - 50 : 0;
        for (int i = startIdx; i < (int)entries.size(); ++i) {
            const auto& e = entries[i];
            std::string ker = e.kernelBase64.size() > 40
                              ? e.kernelBase64.substr(0, 40) + "..."
                              : e.kernelBase64;
            std::string trap = e.trapCode.empty() ? "none" : e.trapCode;
            ImVec4 col = e.trapCode.empty()
                         ? ImVec4{ 0.29f, 0.87f, 0.38f, 1.0f }
                         : ImVec4{ 0.96f, 0.26f, 0.21f, 1.0f };
            ImGui::TextColored(col, "%-6d  %-20s  %s",
                               e.generation, trap.c_str(), ker.c_str());
        }
        ImGui::EndChild();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Performance charts ────────────────────────────────────────────────────
    {
        const auto& lossH = app.trainer().lossHistory();
        const auto& predH = app.trainer().predictionHistory();
        const auto& rewH  = app.trainer().rewardHistory();
        int count = (int)lossH.size();

        if (count > 1) {
            ImGui::TextDisabled("MODEL PERFORMANCE");
            ImGui::Spacing();

            float chartH = 100.0f * m_uiScale;
            float chartW = (panelW - 30.0f) * 0.5f;

            // copy deques to contiguous arrays for ImGui::PlotLines
            std::vector<float> lossArr(lossH.begin(), lossH.end());
            std::vector<float> predArr(predH.begin(), predH.end());
            std::vector<float> rewArr(rewH.begin(), rewH.end());

            // ── Loss chart (left) ────────────────────────────────────────────
            ImGui::BeginChild("##LossChart", { chartW, chartH + 26.0f * m_uiScale }, true);
            ImGui::TextDisabled("LOSS (EMA)");
            float maxLoss = *std::max_element(lossArr.begin(), lossArr.end());
            if (maxLoss < 1e-7f) maxLoss = 1.0f;
            char lossOverlay[48];
            std::snprintf(lossOverlay, sizeof(lossOverlay), "%.6f", lossArr.back());
            ImGui::PushStyleColor(ImGuiCol_PlotLines, { 0.96f, 0.26f, 0.21f, 1.0f });
            ImGui::PlotLines("##loss", lossArr.data(), count, 0,
                             lossOverlay, 0.0f, maxLoss * 1.1f,
                             { chartW - 16.0f, chartH - 10.0f * m_uiScale });
            ImGui::PopStyleColor();
            ImGui::EndChild();

            ImGui::SameLine();

            // ── Prediction vs Reward chart (right) ───────────────────────────
            ImGui::BeginChild("##PredChart", { chartW, chartH + 26.0f * m_uiScale }, true);
            ImGui::TextDisabled("PREDICTION vs REWARD");
            char predOverlay[48];
            std::snprintf(predOverlay, sizeof(predOverlay), "pred=%.3f rew=%.3f",
                          predArr.back(), rewArr.back());
            // prediction line (cyan)
            ImGui::PushStyleColor(ImGuiCol_PlotLines, { 0.11f, 0.83f, 0.93f, 1.0f });
            ImGui::PlotLines("##pred", predArr.data(), count, 0,
                             predOverlay, 0.0f, 1.1f,
                             { chartW - 16.0f, chartH - 10.0f * m_uiScale });
            ImGui::PopStyleColor();
            // overlay reward line (green) on same region — use SetCursorPos
            // to rewind to the same chart origin.
            ImVec2 cur = ImGui::GetCursorPos();
            ImGui::SetCursorPosY(cur.y - (chartH - 10.0f * m_uiScale));
            ImGui::PushStyleColor(ImGuiCol_PlotLines, { 0.29f, 0.87f, 0.38f, 0.6f });
            ImGui::PlotLines("##rew", rewArr.data(), count, 0,
                             nullptr, 0.0f, 1.1f,
                             { chartW - 16.0f, chartH - 10.0f * m_uiScale });
            ImGui::PopStyleColor();
            ImGui::EndChild();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }
    }

    // if training completes *and* the model checkpoint has finished, then
    // switch back to evolution.  we delay the transition when the save
    // countdown is active so that the progress bar remains visible.
    if (app.trainingDone() && !app.savingModel()) {
        app.enableEvolution();
        m_scene = GuiScene::EVOLUTION;
        // fall through; we still want to draw status bar below
    }

    ImGui::EndChild();

    // ── Status bar ────────────────────────────────────────────────────────────
    ImGui::SetCursorPosY((float)winH - 30.0f * m_uiScale);
    ImGui::Separator();
    float t2 = (float)ImGui::GetTime();
    int blinkOn = (int)(t2 * 2.0f) % 2;
    if (blinkOn)
        ImGui::TextColored({ 0.30f, 0.85f, 0.40f, 0.7f },
                           "quine-grub-wasm_sys v2.4  ///  TRAINING ACTIVE");
    else
        ImGui::TextDisabled("quine-grub-wasm_sys v2.4  ///  TRAINING ACTIVE");

    if (m_monoFont) ImGui::PopFont();
    ImGui::End();
}

// ─── Panel implementations ───────────────────────────────────────────────────

void Gui::renderTopBar(App& app, int winW) {
    ImGui::Separator();
    ImGui::TextColored({ 0.11f, 0.83f, 0.93f, 1.0f }, "quine-grub-wasm v2.4");
    ImGui::SameLine(0, 30);
    ImGui::SameLine(0, 20);
    ImGui::Text("GEN: %04d", app.generation());
    ImGui::SameLine(0, 20);
    ImGui::TextColored(colorForState(app.state()), "STATE: %s",
                       stateStr(app.state()).c_str());
    ImGui::SameLine(0, 20);
    ImGui::Text("UPTIME: %.1fs", app.uptimeSec());
    ImGui::SameLine(0, 20);
    if (app.retryCount() > 0)
        ImGui::TextColored({0.96f, 0.26f, 0.21f, 1}, "RETRIES: %d",
                           app.retryCount());

    ImGui::SameLine();
    float btnW = 140.0f * m_uiScale;
    ImGui::SetCursorPosX((float)winW - btnW - 110);
    if (app.isPaused()) {
        ImGui::PushStyleColor(ImGuiCol_Button, {0.4f, 0.3f, 0.0f, 1});
        if (ImGui::Button("RESUME SYSTEM", {btnW, 0})) app.togglePause();
        ImGui::PopStyleColor();
    } else {
        if (ImGui::Button("PAUSE  SYSTEM", {btnW, 0})) app.togglePause();
    }
    ImGui::SameLine();
    if (ImGui::Button("Advisor", {btnW, 0})) {
        m_showAdvisor = !m_showAdvisor;
    }
    ImGui::SameLine();
    // EXPORT button removed – telemetry is now saved automatically every
    // generation under bin/seq/<runid>/gen_<n>.txt (see App autoExport()).
    ImGui::Separator();
}

// additional panels moved from the original window.cpp

void Gui::renderLogPanel(const App& app, float w, float h) {
    ImGui::BeginChild("##LogPanel", { w, h }, true,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::TextDisabled("SYSTEM LOG  BUF:%d", (int)app.logs().size());
    // filter input
    ImGui::SameLine();
    ImGui::SetCursorPosX(w - 200.0f * m_uiScale);
    // InputText doesn't accept std::string in this build, so use fixed buffer.
    char buf[256];
    std::strncpy(buf, m_logFilter.c_str(), sizeof(buf));
    if (ImGui::InputText("Filter", buf, sizeof(buf))) {
        m_logFilter = buf;
    }

    ImGui::Separator();
    ImGui::BeginChild("##LogScroll", { 0, 0 }, false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& log : app.logs()) {
        if (!m_logFilter.empty()) {
            if (log.message.find(m_logFilter) == std::string::npos)
                continue;
        }
        uint64_t t = log.timestamp;
        int ms = (int)(t % 1000);
        int s  = (int)((t / 1000)   % 60);
        int m  = (int)((t / 60000)  % 60);
        int hr = (int)(t / 3600000);
        char ts[20];
        std::snprintf(ts, sizeof ts, "%02d:%02d:%02d.%03d", hr, m, s, ms);
        ImGui::TextDisabled("%s", ts);
        ImGui::SameLine();
        if (log.type == "system")
            ImGui::TextColored(colorForLogType(log.type), "-> %s",
                               log.message.c_str());
        else
            ImGui::TextColored(colorForLogType(log.type), "%s",
                               log.message.c_str());
    }
    if (m_scrollLogs) {
        ImGui::SetScrollHereY(1.0f);
        m_scrollLogs = false;
    }
    constexpr float kScrollTolerance = 5.0f;
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - kScrollTolerance)
        m_scrollLogs = true;
    ImGui::EndChild();
    ImGui::EndChild();
}

void Gui::renderInstrPanel(const App& app, float w, float h) {
    ImGui::BeginChild("##InstrPanel", { w, h }, true,
                      ImGuiWindowFlags_NoScrollbar);
    int ip = app.programCounter();
    ImGui::TextDisabled("INSTRUCTION STACK");
    ImGui::SameLine();
    if (ip >= 0) ImGui::TextColored({0.29f, 0.87f, 0.38f, 1}, "IP:%03d", ip);
    else         ImGui::TextDisabled("IP:WAIT");
    ImGui::Separator();

    ImGui::BeginChild("##InstrScroll", { 0, 0 }, false);
    const auto& instrs = app.instructions();
    for (int i = 0; i < (int)instrs.size(); i++) {
        const auto& inst   = instrs[i];
        bool        active = (i == ip);
        std::string name   = getOpcodeName(inst.opcode);

        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
            ImGui::TextUnformatted("-> ");
            ImGui::SameLine();
        } else {
            ImGui::TextDisabled("   ");
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 180));
        }

        if (inst.argLen > 0) {
            char argBuf[64] = {};
            int pos = 0;
            for (int ai = 0; ai < inst.argLen && pos < 58; ++ai)
                pos += std::snprintf(argBuf + pos, sizeof(argBuf) - pos,
                                     " 0x%02X", (unsigned)inst.args[ai]);
            ImGui::Text("%-12s%s", name.c_str(), argBuf);
        } else {
            ImGui::TextUnformatted(name.c_str());
        }
        ImGui::PopStyleColor();

        if (active && m_scrollInstrs) {
            ImGui::SetScrollHereY(0.5f);
            m_scrollInstrs = false;
        }
    }
    if (instrs.empty())
        ImGui::TextDisabled("Waiting for Kernel...");
    ImGui::EndChild();
    ImGui::EndChild();
}

void Gui::renderKernelPanel(const App& app, float w, float h) {
    ImGui::BeginChild("##KernelPanel", { w, h }, true,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::TextDisabled("KERNEL SOURCE (BASE64)");
    ImGui::SameLine();
    if (ImGui::SmallButton("COPY"))
        ImGui::SetClipboardText(app.currentKernel().c_str());
    ImGui::Separator();

    ImGui::TextColored({0.13f, 0.83f, 0.93f, 1}, "%zuB",  app.kernelBytes());
    ImGui::SameLine(70);
    ImGui::TextColored({0.78f, 0.5f,  0.98f, 1}, "+%d OPS", app.evolutionAttempts());
    ImGui::SameLine(160);
    ImGui::TextColored({0.29f, 0.87f, 0.38f, 1}, "%d PAT", app.knownInstructionCount());
    ImGui::Separator();

    ImGui::BeginChild("##KernelScroll", { 0, 0 }, false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    {
        const std::string& cur    = app.currentKernel();
        const std::string& stable = app.stableKernel();
        const auto&        instrs = app.instructions();

        int codeStartByte = 0;
        if (!instrs.empty() && instrs[0].originalOffset > 0)
            codeStartByte = instrs[0].originalOffset;
        int codeStartChar = (int)(codeStartByte * 4 / 3);

        ImDrawList* dl     = ImGui::GetWindowDrawList();
        ImVec2      cursor = ImGui::GetCursorScreenPos();
        float       charW  = ImGui::CalcTextSize("A").x;
        float       lineH  = ImGui::GetTextLineHeightWithSpacing();
        float       startX = cursor.x;
        float       maxX   = cursor.x + w - 20;
        float x = startX, y = cursor.y;

        for (int i = 0; i < (int)cur.size(); i++) {
            ImVec4 col;
            if      (i < codeStartChar)         col = { 0.31f, 0.44f, 0.70f, 0.6f };
            else if (i >= (int)stable.size())   col = { 0.29f, 0.87f, 0.38f, 1.0f };
            else if (cur[i] != stable[i])       col = { 0.98f, 0.82f, 0.10f, 1.0f };
            else                                col = { 0.44f, 0.44f, 0.44f, 0.8f };

            char ch[2] = { cur[i], 0 };
            dl->AddText({ x, y },
                IM_COL32((int)(col.x*255),(int)(col.y*255),
                         (int)(col.z*255),(int)(col.w*255)),
                ch);
            x += charW;
            if (x + charW > maxX) { x = startX; y += lineH; }
        }

        ImGui::Dummy({ w - 20, y - cursor.y + lineH + 4 });
        ImGui::TextDisabled("--- END OF FILE ---");
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

void Gui::renderAdvisorPanel(const App& app, float w, float h) {
    ImGui::BeginChild("##AdvisorPanel", { w, h }, true);
    ImGui::TextDisabled("ADVISOR STATE");
    ImGui::Separator();
    ImGui::Text("Entries: %zu", app.advisor().entryCount());
    if (ImGui::Button("Dump state")) {
        std::string path = "advisor_dump.txt";
        if (app.advisor().dump(path)) {
            m_lastDumpPath = path;
        } else {
            m_lastDumpPath = "<error>";
        }
    }
    if (!m_lastDumpPath.empty()) {
        ImGui::Text("Last dump: %s", m_lastDumpPath.c_str());
    }
    ImGui::EndChild();
}

void Gui::renderInstancesPanel(App& app, float w, float h) {
    ImGui::BeginChild("##InstancesPanel", { w, h }, true);
    ImGui::TextDisabled("INSTANCES");
    ImGui::Separator();
    for (int i = 0; i < app.instanceCount(); i++) {
        const std::string& inst = app.instances()[i];
        ImGui::Text("%d: %s", i, inst.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton((std::string("Kill##") + std::to_string(i)).c_str())) {
            app.killInstance(i);
        }
    }
    ImGui::EndChild();
}

// Render heatmaps of each policy layer's weight matrix.  Each layer is shown
// as a small grid of colored cells (red for positive weights, blue for
// negative) stacked vertically.  This panel is drawn in the evolution scene
// above the heap memory heatmap.
void Gui::renderWeightHeatmaps(const App& app, int winW) {
    const Policy& pol = app.trainer().policy();
    int layers = pol.layerCount();
    if (layers == 0) return;

    // Rebuild cache if the generation changed or the network layout differs
    if (app.generation() != m_lastHeatmapGen ||
        app.trainer().observations() != m_lastHeatmapObs ||
        (int)m_heatmapCache.size() != layers) {
        // free existing textures first
        for (auto &c : m_heatmapCache) {
            if (c.tex) SDL_DestroyTexture(c.tex);
        }
        m_heatmapCache.clear();
        m_heatmapCache.resize(layers);
        m_lastHeatmapGen = app.generation();
        m_lastHeatmapObs = app.trainer().observations();

        // create a pixel buffer and texture for each layer
        for (int l = 0; l < layers; ++l) {
            int in = pol.layerInSize(l);
            int out = pol.layerOutSize(l);
            if (in <= 0 || out <= 0) continue;
            std::vector<uint32_t> pixels(in * out);
            // we will map weight->color once here
            // use SDL to mapRGBA so we don't make assumptions about byte order.
        // SDL3 switched to PixelFormatDetails; these are cached internally,
        // so no allocation or free is necessary.
        for (int i = 0; i < out; ++i) {
            for (int j = 0; j < in; ++j) {
                float val = pol.layerWeights(l)[i * in + j];
                float tn = std::tanh(val);
                uint8_t r = 0, g = 0, b = 0, a = 255;
                if (tn >= 0) {
                    r = static_cast<uint8_t>(tn * 255);
                } else {
                    b = static_cast<uint8_t>(-tn * 255);
                }
                // direct RGBA32 pixel encoding (avoids SDL palette API quirks)
                pixels[i * in + j] = ((uint32_t)r << 24) | ((uint32_t)g << 16)
                                   | ((uint32_t)b << 8)  | (uint32_t)a;
            }
        }
            SDL_Texture* tex = SDL_CreateTexture(
                m_renderer,
                SDL_PIXELFORMAT_RGBA32,
                SDL_TEXTUREACCESS_STATIC,
                in, out);
            if (tex) {
                SDL_UpdateTexture(tex, nullptr, pixels.data(), in * sizeof(uint32_t));
                m_heatmapCache[l].tex = tex;
                m_heatmapCache[l].w   = in;
                m_heatmapCache[l].h   = out;
            }
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("NN WEIGHT HEATMAPS");
    float visW = (float)winW - 20.0f;

    // Layout: distribute available width proportionally among layers;
    // each layer's share is proportional to its weight count.  The panel
    // has a fixed height so the textures render at a readable size.
    const float panelH = 100.0f;  // target panel height in pixels
    const float pad    = 4.0f;    // gap between layer images

    // compute total weight count for proportional sizing
    int totalWeights = 0;
    for (int l = 0; l < layers; ++l) {
        int in = pol.layerInSize(l);
        int out = pol.layerOutSize(l);
        if (in > 0 && out > 0) totalWeights += in * out;
    }
    if (totalWeights <= 0) return;

    float usableW = visW - pad * (float)(layers - 1);
    if (usableW < 10.0f) usableW = 10.0f;

    ImGui::BeginChild("##WeightHeatmaps", ImVec2(visW, panelH + 10.0f),
                      true, ImGuiWindowFlags_NoScrollbar);
    for (int l = 0; l < layers; ++l) {
        int in = pol.layerInSize(l);
        int out = pol.layerOutSize(l);
        if (in <= 0 || out <= 0) continue;
        float fraction = (float)(in * out) / (float)totalWeights;
        float drawW = usableW * fraction;
        if (drawW < 4.0f) drawW = 4.0f;
        float drawH = panelH;
        ImGui::BeginGroup();
        if (m_heatmapCache[l].tex) {
            ImGui::Image((ImTextureID)m_heatmapCache[l].tex,
                         ImVec2(drawW, drawH - 14.0f));
        } else {
            ImGui::Dummy(ImVec2(drawW, drawH - 14.0f));
        }
        ImGui::TextDisabled("L%d", l);
        ImGui::EndGroup();
        ImGui::SameLine(0, pad);
    }
    ImGui::EndChild();
}

void Gui::renderStatusBar(const App& app) {
    ImGui::Separator();
    ImGui::TextDisabled("quine-grub-wasm_sys v2.4 // STATUS: %s",
                        app.isPaused() ? "PAUSED" : "RUNNING");
    if (app.instanceCount() > 0) {
        ImGui::SameLine(0,20);
        ImGui::Text("Instances: %d", app.instanceCount());
    }
    // show fps at far right
    ImGui::SameLine(0,20);
    ImGui::Text("FPS: %.1f", m_fps);
}
