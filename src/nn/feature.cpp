#include "nn/feature.h"
#include "base64.h"

#include <algorithm>
#include <array>
#include <cmath>

std::vector<float> Feature::extract(const TelemetryEntry& entry) {
    std::vector<float> vec(kFeatSize, 0.0f);
    if (entry.kernelBase64.empty()) return vec;

    auto bytes  = base64_decode(entry.kernelBase64);
    auto instrs = extractCodeSection(bytes);

    if (instrs.empty()) {
        // no code found — flag trap and return zeros elsewhere
        if (!entry.trapCode.empty()) vec[256] = 1.0f;
        return vec;
    }

    const float denom = static_cast<float>(instrs.size());

    // ── slots 0-255: normalised opcode frequency histogram ───────────────────
    for (auto& inst : instrs)
        vec[inst.opcode] += 1.0f;
    for (int i = 0; i < 256; ++i)
        vec[i] /= denom;

    // ── slots 256-511: bigram (opcode pair) frequency histogram ──────────────
    // Hash consecutive opcode pairs into 256 buckets.
    if (instrs.size() > 1) {
        for (size_t i = 0; i + 1 < instrs.size(); ++i) {
            int bucket = (int)((instrs[i].opcode * 31u + instrs[i+1].opcode) & 0xFF);
            vec[256 + bucket] += 1.0f;
        }
        const float bigramDenom = denom - 1.0f;
        for (int i = 256; i < 512; ++i)
            vec[i] /= bigramDenom;
    }

    // ── slots 512-639: structural / metadata features ────────────────────────
    // Compute several structural signals in one pass.
    int controlOps = 0;  // block/branch/return/call
    int memOps     = 0;  // load/store
    int arithOps   = 0;  // i32 arithmetic
    int localOps   = 0;  // local.get / local.set / local.tee
    int dropCount  = 0;  // drop (0x1A)
    int maxRunLen  = 1;
    int curRun     = 1;

    for (size_t i = 0; i < instrs.size(); ++i) {
        uint8_t op = instrs[i].opcode;
        // control flow: 0x00-0x11
        if (op <= 0x11) ++controlOps;
        // memory: 0x28-0x3E
        else if (op >= 0x28 && op <= 0x3E) ++memOps;
        // i32 arithmetic: 0x6A-0x8A
        else if (op >= 0x6A && op <= 0x8A) ++arithOps;
        // locals: 0x20,0x21,0x22
        else if (op == 0x20 || op == 0x21 || op == 0x22) ++localOps;
        // drop
        if (op == 0x1A) ++dropCount;

        if (i > 0 && instrs[i].opcode == instrs[i-1].opcode) {
            ++curRun;
            if (curRun > maxRunLen) maxRunLen = curRun;
        } else {
            curRun = 1;
        }
    }

    auto norm = [&](float v, float scale) {
        return std::min(1.0f, v / (denom * scale + 1e-6f));
    };

    vec[512] = !entry.trapCode.empty() ? 1.0f : 0.0f;                   // trap flag
    vec[513] = std::min(1.0f, static_cast<float>(entry.generation) / 1000.0f); // generation progress
    vec[514] = std::min(1.0f, static_cast<float>(bytes.size()) / 4096.0f);     // kernel size
    vec[515] = norm(controlOps, 0.3f);                                   // control density
    vec[516] = norm(memOps,     0.2f);                                   // memory density
    vec[517] = norm(arithOps,   0.4f);                                   // arithmetic density
    vec[518] = norm(localOps,   0.15f);                                  // local-var density
    vec[519] = norm(dropCount,  0.2f);                                   // drop density
    vec[520] = std::min(1.0f, static_cast<float>(maxRunLen) / 16.0f);   // longest opcode run
    vec[521] = std::min(1.0f, denom / 256.0f);                          // instruction count

    // Unique opcode ratio (diversity)
    std::array<bool,256> seen{};
    int unique = 0;
    for (auto& inst : instrs)
        if (!seen[inst.opcode]) { seen[inst.opcode] = true; ++unique; }
    vec[522] = static_cast<float>(unique) / 256.0f;

    // Average argument length (instruction complexity)
    float avgArgLen = 0.0f;
    for (auto& inst : instrs) avgArgLen += inst.argLen;
    avgArgLen /= denom;
    vec[523] = std::min(1.0f, avgArgLen / 4.0f);

    // slots 524-639 remain zero (reserved for future use)

    // ── slots 640-1023: raw opcode sequence encoding (one-hot windows) ────────
    // Encode first 96 opcodes as one-hot in a 4-per-slot compact form;
    // this gives the network short-range positional context.
    const int seqSlots = kFeatSize - 640;        // 384
    const int maxOps   = std::min((int)instrs.size(), seqSlots);
    for (int i = 0; i < maxOps; ++i) {
        // Normalise to [0,1] by dividing opcode byte by 255
        vec[640 + i] = instrs[i].opcode / 255.0f;
    }

    return vec;
}

std::vector<uint8_t> Feature::extractSequence(const TelemetryEntry& entry) {
    if (entry.kernelBase64.empty()) return {};
    auto bytes = base64_decode(entry.kernelBase64);
    return extractCodeSectionOpcodes(bytes);
}
