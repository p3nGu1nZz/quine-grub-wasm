#pragma once

#include "nn/advisor.h"
#include "wasm/parser.h"
#include <vector>

// Feature vector size: 256 opcode-frequency slots (indices 0-255) plus
// 768 reserved slots for future features, giving a total of 1024 features.
static constexpr int kFeatSize = 1024;

// Convert a telemetry entry (kernel/mutation) into a fixed-length
// numeric feature vector suitable for feeding into a neural policy.
// Current implementation counts opcode frequencies in the kernel.
class Feature {
public:
    // return vector of kFeatSize floats (opcode histogram + extra features)
    static std::vector<float> extract(const TelemetryEntry& entry);

    // decode the kernel and return the raw opcode sequence (one byte per
    // instruction).  Useful for sequence-based models and training.
    static std::vector<uint8_t> extractSequence(const TelemetryEntry& entry);
};
