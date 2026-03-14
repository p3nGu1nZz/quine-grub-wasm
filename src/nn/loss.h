#pragma once

// Compute a loss value from telemetry information.
// Currently the loss is simply the negative of the generation count (i.e.
// reward = generations, loss = -reward).  More complex heuristics may be
// added later.

struct TelemetryEntry;

class Loss {
public:
    static float compute(const TelemetryEntry& entry);
};
