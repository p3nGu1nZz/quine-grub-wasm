#pragma once

#include "app.h"

#include <imgui.h>
#include <vector>

// ── GuiHeatmap ────────────────────────────────────────────────────────────────
//
// Renders the SYSTEM_MEMORY_MAP / HEAP_VISUALIZER panel: a grid of small
// heat-decaying blocks that reflect which WASM memory regions are active.
//
// Call renderHeatmap() from inside an ImGui child window or at the bottom of the
// main ImGui window after calling ImGui::GetWindowDrawList().
// ─────────────────────────────────────────────────────────────────────────────

class GuiHeatmap {
public:
    // Draw the memory heatmap into draw list dl at pos/size.
    // Call once per frame (handles heat decay internally).
    void draw(const App& app, ImDrawList* dl, ImVec2 pos, ImVec2 size);

    // Render the full panel (header label + background rect + heatmap).
    void renderPanel(const App& app, int winW);

private:
    // Per-block heat values (0..1); decays each frame.
    std::vector<float> m_heatMap;
};
