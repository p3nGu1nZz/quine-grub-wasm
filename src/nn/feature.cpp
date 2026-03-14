#include "nn/feature.h"
#include "base64.h"

std::vector<float> Feature::extract(const TelemetryEntry& entry) {
    std::vector<float> vec(kFeatSize, 0.0f);
    if (entry.kernelBase64.empty()) return vec;

    auto bytes = base64_decode(entry.kernelBase64);
    auto instrs = extractCodeSection(bytes);
    for (auto& inst : instrs) {
        uint8_t op = inst.opcode;
        vec[op] += 1.0f;  // indices 0-255: opcode frequency counts
        // indices 256-1023: reserved for future features (currently zero)
    }
    // simple supplemental feature: if this entry contained a trap code, set
    // a flag in the first spare slot.  This is part of the "feature
    // iteration" task from issue #101.
    if (!entry.trapCode.empty() && kFeatSize > 256) {
        vec[256] = 1.0f;
    }
    return vec;
}

std::vector<uint8_t> Feature::extractSequence(const TelemetryEntry& entry) {
    if (entry.kernelBase64.empty()) return {};
    auto bytes = base64_decode(entry.kernelBase64);
    return extractCodeSectionOpcodes(bytes);
}
