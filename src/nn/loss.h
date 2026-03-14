#pragma once

// Loss function for the quine evolution trainer.
// Returns a scalar "badness" score: lower is better, negative means good.
//
// Components:
//   - Primary: negative generation count (more generations = lower loss)
//   - Trap penalty:  +8 when the kernel triggered a trap
//   - Drop ratio penalty: penalises sequences with >40% DROP opcodes
//   - Diversity penalty: penalises low unique-opcode count
//   - Length penalties/rewards: discourages degenerate short sequences

struct TelemetryEntry;

class Loss {
public:
    static float compute(const TelemetryEntry& entry);
};
