#pragma once

#include "nn/policy.h"
#include "nn/advisor.h"

// Trainer applies online updates to a policy network given telemetry data.
class Trainer {
public:
    Trainer();

    // observe one telemetry entry and adjust weights accordingly
    void observe(const TelemetryEntry& entry);

    // save/load model state (weights) to disk
    bool save(const std::string& path) const;
    bool load(const std::string& path);

    // expose access to underlying policy for inspection/tests
    const Policy& policy() const { return m_policy; }

    // training statistics
    int   observations() const { return m_observations; }
    float avgLoss()      const { return m_avgLoss; }
    float lastLoss()     const { return m_lastLoss; }

    // reset statistics and replay buffer without touching weights.  called
    // when we start a fresh training cycle (e.g. after an evolution run) so
    // that stale loss values or old entries don't skew the next phase.
    void reset();

    // testing hooks
    bool test_lastUsedSequence() const { return m_lastUsedSequence; }
    int  test_replaySize() const { return (int)m_replayBuffer.size(); }
    void test_setReplayCap(size_t c) { m_replayCap = c; }

private:
    // perform a weight-update for a single telemetry entry; factored out so
    // we can call it for replay-buffer samples as well.
    void trainOnEntry(const TelemetryEntry& entry);

    // true if last observe() call processed a non-empty opcode sequence
    bool m_lastUsedSequence = false;
    Policy m_policy;
    int   m_observations = 0;
    float m_avgLoss      = 0.0f;
    float m_lastLoss     = 0.0f;
    float m_maxReward    = 1.0f; // tracks max reward seen for normalisation

    // replay buffer stores recent telemetry entries for mini-batch training
    std::vector<TelemetryEntry> m_replayBuffer;
    size_t m_replayCap = 256; // default capacity; can be tuned
};
