#pragma once

#include "policy.h"
#include "advisor.h"
#include <deque>

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
    float maxReward()    const { return m_maxReward; }

    // reset statistics and replay buffer; does not alter network weights.
    // used when kicking off a new training phase so old loss averages and
    // entries cannot influence the fresh cycle.
    void reset();

    // rolling history for chart rendering (most-recent kHistoryCap points)
    static constexpr int kHistoryCap = 256;
    const std::deque<float>& lossHistory()       const { return m_lossHistory; }
    const std::deque<float>& predictionHistory() const { return m_predHistory; }
    const std::deque<float>& rewardHistory()      const { return m_rewardHistory; }

    // testing hooks
    bool test_lastUsedSequence() const { return m_lastUsedSequence; }
    int  test_replaySize() const { return (int)m_replayBuffer.size(); }
    void test_setReplayCap(size_t c) {
        m_replayCap = c;
        // trim existing entries if the cap was lowered
        while (m_replayBuffer.size() > m_replayCap) {
            m_replayBuffer.erase(m_replayBuffer.begin());
        }
    }

private:
    // perform a weight-update for a single telemetry entry; factored out so
    // we can call it for replay-buffer samples as well.
    void trainOnEntry(const TelemetryEntry& entry);

    // ── Hyper-parameters ─────────────────────────────────────────────────────
    static constexpr float kLearningRate = 0.004f;   // base SGD learning rate
    static constexpr float kEmaDecay     = 0.90f;    // EMA decay for avgLoss
    static constexpr float kGradClip     = 0.5f;     // max absolute gradient
    static constexpr float kWeightDecay  = 2e-5f;    // L2 regularisation

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

    // rolling history deques for GUI charts
    std::deque<float> m_lossHistory;
    std::deque<float> m_predHistory;
    std::deque<float> m_rewardHistory;

    // internal: push a value into a capped deque
    void pushHistory(std::deque<float>& q, float v) {
        q.push_back(v);
        if ((int)q.size() > kHistoryCap) q.pop_front();
    }
};
