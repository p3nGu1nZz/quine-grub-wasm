#include "nn/loss.h"
#include "nn/advisor.h"
#include "wasm/parser.h"
#include "base64.h"

#include <algorithm>
#include <array>
#include <cmath>

float Loss::compute(const TelemetryEntry& entry) {
    // ── Primary signal: generation count (higher = better, so we negate) ─────
    float loss = -static_cast<float>(entry.generation);

    // ── Trap penalty: trapping kernels are worse than non-trapping ones ───────
    if (!entry.trapCode.empty())
        loss += 8.0f;

    // ── Sequence-quality heuristics ────────────────────────────────────────────
    const auto& seq = entry.opcodeSequence;
    if (!seq.empty()) {
        const float len = static_cast<float>(seq.size());

        // Penalise excessive DROP instructions (wasteful mutations)
        int drops = 0;
        for (uint8_t op : seq) if (op == 0x1A) ++drops;
        float dropRatio = drops / len;
        if (dropRatio > 0.4f) loss += (dropRatio - 0.4f) * 5.0f;

        // Penalise low opcode diversity (stuck mutations produce repetitive code)
        std::array<bool,256> seen{};
        int unique = 0;
        for (uint8_t op : seq) if (!seen[op]) { seen[op] = true; ++unique; }
        float diversity = static_cast<float>(unique) / 64.0f; // expect at least 64 distinct ops
        if (diversity < 1.0f) loss += (1.0f - diversity) * 3.0f;

        // Penalise very short instruction sequences (may indicate degenerate kernels)
        if (len < 8.0f) loss += (8.0f - len) * 0.5f;

        // Mild reward for longer, richer kernels (exploration incentive)
        if (len > 32.0f) loss -= std::min(2.0f, (len - 32.0f) / 64.0f);
    }

    return loss;
}
