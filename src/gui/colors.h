#pragma once

#include "types.h"

#include <imgui.h>
#include <string>

// ── colors.h ──────────────────────────────────────────────────────────────────
//
// Small, header-only helpers that map simulation states / eras / log types
// to Dear ImGui color values.  Included by gui.cpp and heatmap.cpp.
// ─────────────────────────────────────────────────────────────────────────────

inline ImVec4 colorForLogType(const std::string& t) {
    if (t == "success") return { 0.29f, 0.87f, 0.38f, 1.0f };
    if (t == "warning") return { 0.98f, 0.82f, 0.10f, 1.0f };
    if (t == "error")   return { 0.96f, 0.26f, 0.21f, 1.0f };
    if (t == "system")  return { 0.11f, 0.83f, 0.93f, 1.0f };
    if (t == "mutation")return { 0.78f, 0.50f, 0.98f, 1.0f };
    return { 0.63f, 0.63f, 0.63f, 1.0f }; // info
}

inline ImVec4 colorForState(SystemState s) {
    switch (s) {
        case SystemState::BOOTING:         return { 0.98f, 0.82f, 0.10f, 1.0f };
        case SystemState::LOADING_KERNEL:  return { 0.39f, 0.66f, 0.97f, 1.0f };
        case SystemState::EXECUTING:       return { 0.29f, 0.87f, 0.38f, 1.0f };
        case SystemState::VERIFYING_QUINE: return { 0.78f, 0.50f, 0.98f, 1.0f };
        case SystemState::SYSTEM_HALT:     return { 0.96f, 0.26f, 0.21f, 1.0f };
        case SystemState::REPAIRING:       return { 0.99f, 0.55f, 0.19f, 1.0f };
        default:                           return { 0.44f, 0.44f, 0.44f, 1.0f };
    }
}

// Era-based theming removed — use neutral defaults for background and accents.

