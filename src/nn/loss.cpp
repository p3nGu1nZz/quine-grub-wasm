#include "nn/loss.h"
#include "nn/advisor.h"

float Loss::compute(const TelemetryEntry& entry) {
    // negative generation as loss; more features may be added later
    return -static_cast<float>(entry.generation);
}
