#include "gui/heatmap.h"
#include "gui/colors.h"
#include "util.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>
#include <iomanip>

// Per-instance random helper
static int hmRandInt(int n) {
    static thread_local std::mt19937 rng(std::random_device{}());
    return std::uniform_int_distribution<int>(0, n - 1)(rng);
}

void GuiHeatmap::draw(const App& app, ImDrawList* dl, ImVec2 pos, ImVec2 size) {
    int kernelSz = static_cast<int>(app.kernelBytes());
    if (kernelSz == 0) return;

    const int BLOCK  = kernelSz < 256 ? 8 : kernelSz < 1024 ? 5 : 3;
    const int GAP    = 1;
    const int STEP   = BLOCK + GAP;
    const int BPB    = kernelSz < 256 ? 1 : kernelSz < 1024 ? 4 : 16;
    const int COLS   = std::max(1, (int)(size.x / STEP));
    const int BLOCKS = (kernelSz + BPB - 1) / BPB;

    if (static_cast<int>(m_heatMap.size()) != BLOCKS)
        m_heatMap.assign(BLOCKS, 0.0f);

    // Era-based theming removed — use neutral theme and active color
    ImVec4 theme   = { 0.07f, 0.16f, 0.23f, 1 };
    ImVec4 activeC = { 0.13f, 0.83f, 0.93f, 1 };

    bool isActive = (app.state() == SystemState::LOADING_KERNEL ||
                     app.state() == SystemState::EXECUTING);

    for (int i = 0; i < BLOCKS; i++) {
        int   col    = i % COLS;
        int   row    = i / COLS;
        float x      = pos.x + col * STEP;
        float y      = pos.y + row * STEP;
        if (y + BLOCK > pos.y + size.y) break;

        int  bStart  = i * BPB;
        int  bEnd    = bStart + BPB;
        bool focused = isActive &&
            (bStart < app.focusAddr() + app.focusLen()) &&
            (bEnd   > app.focusAddr());

        if (focused)
            m_heatMap[i] = 1.0f;
        else if (app.isSystemReading() && (hmRandInt(100) > 98))
            m_heatMap[i] = std::min(1.0f, m_heatMap[i] + 0.5f);

        m_heatMap[i] *= 0.85f;
        if (m_heatMap[i] < 0.005f) m_heatMap[i] = 0.0f;

        float heat = m_heatMap[i];

        ImU32 baseCol = IM_COL32(
            (int)(theme.x * 255), (int)(theme.y * 255),
            (int)(theme.z * 255), (int)(0.3f * 255));
        dl->AddRectFilled({ x, y }, { x + BLOCK, y + BLOCK }, baseCol);

        if (heat > 0.01f) {
            float  sz  = BLOCK * (1.0f + heat * 0.6f);
            float  off = (sz - BLOCK) / 2.0f;
            ImVec4 c   = heat > 0.5f
                ? ImVec4(1, 1, 1, heat)
                : ImVec4(activeC.x, activeC.y, activeC.z, heat);
            dl->AddRectFilled(
                { x - off, y - off }, { x - off + sz, y - off + sz },
                IM_COL32((int)(c.x*255),(int)(c.y*255),(int)(c.z*255),(int)(c.w*255)));
        }

        if (app.isMemoryGrowing() && (hmRandInt(100) > 98))
            dl->AddRectFilled({ x, y }, { x + BLOCK, y + BLOCK },
                              IM_COL32(255, 255, 255, 100));
    }
}

void GuiHeatmap::renderPanel(const App& app, int winW) {
    ImGui::Separator();

    std::ostringstream hdr;
    hdr << "SYSTEM_MEMORY_MAP // HEAP_VISUALIZER  PTR:0x"
        << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
        << app.focusAddr();
    if (app.isSystemReading())
        hdr << "  [READ]";
    else if (app.state() == SystemState::LOADING_KERNEL ||
             app.state() == SystemState::EXECUTING)
        hdr << "  [WRITE]";
    hdr << "  SIZE:" << std::dec << app.kernelBytes() << "B";
    ImGui::TextDisabled("%s", hdr.str().c_str());

    ImVec2 visPos = ImGui::GetCursorScreenPos();
    float  visW   = static_cast<float>(winW) - 20.0f;
    float  visH   = 80.0f;
    ImGui::Dummy({ visW, visH });
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(visPos, { visPos.x + visW, visPos.y + visH },
                      IM_COL32(5, 8, 18, 240));
    draw(app, dl, visPos, { visW, visH });
}
