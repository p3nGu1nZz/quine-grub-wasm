#include "train.h"
#include "feature.h"
#include "loss.h"
#include <fstream>
#include <cmath>
#include <random>

// simple random integer in [0,n)
static int hmRandInt(int n) {
    static thread_local std::mt19937 rng(std::random_device{}());
    return std::uniform_int_distribution<int>(0, n - 1)(rng);
}

// ─── Architecture (scaled down) ─────────────────────────────────────────────
// Layer 0 Dense : kFeatSize(1024) → 32     (compact input projection)
// Layer 1 Dense : 32 → 64                    (feed into LSTM)
// Layer 2 LSTM  : 64 → 64                    (temporal context)
// Layer 3 Dense : 64 → 32                    (dimensionality reduction)
// Layer 4 Dense : 32 → 1                     (scalar reward prediction)

Trainer::Trainer() {
    m_policy.addDense(kFeatSize, 32);    // layer 0
    m_policy.addDense(32, 64);           // layer 1
    m_policy.addLSTM(64, 64);            // layer 2
    m_policy.addDense(64, 32);           // layer 3
    m_policy.addDense(32, 1);            // layer 4 (output)
}

void Trainer::observe(const TelemetryEntry& entry) {
    m_observations++;
    if (entry.kernelBase64.empty()) return;

    // figure out which branch we will use for this entry
    auto seq = Feature::extractSequence(entry);
    m_lastUsedSequence = !seq.empty();

    // do the actual weight update for the current example
    trainOnEntry(entry);

    // if we have stored past examples, sample one at random and train on it
    if (!m_replayBuffer.empty()) {
        int idx = hmRandInt((int)m_replayBuffer.size());
        trainOnEntry(m_replayBuffer[idx]);
    }

    // push into replay buffer if sequence-based (otherwise histogram only)
    if (m_lastUsedSequence) {
        m_replayBuffer.push_back(entry);
        if (m_replayBuffer.size() > m_replayCap)
            m_replayBuffer.erase(m_replayBuffer.begin());
    }
}

// helper that encapsulates the existing logic for a single observation; this
// lets us easily re-use it when sampling from the replay buffer.
void Trainer::trainOnEntry(const TelemetryEntry& entry) {
    // Reward signal: generation count (higher = kernel survived longer = better)
    float reward = static_cast<float>(entry.generation);
    if (reward > m_maxReward) m_maxReward = reward;
    float normReward = m_maxReward > 0.0f ? reward / m_maxReward : 0.0f;

    const float lr = 0.005f;
    if (m_lastUsedSequence) {
        auto seq = Feature::extractSequence(entry);
        m_policy.resetState();
        for (auto op : seq) {
            std::vector<float> features(kFeatSize, 0.0f);
            if (op < kFeatSize) features[op] = 1.0f;

            std::vector<std::vector<float>> acts;
            m_policy.forwardActivations(features, acts);
            float prediction = acts.back().empty() ? 0.0f : acts.back()[0];
            float diff = prediction - normReward;
            m_lastLoss = diff * diff;
            m_avgLoss = m_avgLoss * 0.9f + m_lastLoss * 0.1f;

            // only update non-LSTM layers for simplicity
            for (int l = 0; l < m_policy.layerCount(); ++l) {
                if (m_policy.layerType(l) == Policy::LayerType::LSTM) continue;
                const int inl  = m_policy.layerInSize(l);
                const int outl = m_policy.layerOutSize(l);
                const auto& input_act = acts[l];
                auto w = m_policy.layerWeights(l);
                for (int o = 0; o < outl; ++o)
                    for (int i = 0; i < inl; ++i)
                        w[o * inl + i] -= lr * diff * input_act[i];
                m_policy.setLayerWeights(l, w);
            }
        }
    } else {
        auto features = Feature::extract(entry);

        std::vector<std::vector<float>> acts;
        m_policy.forwardActivations(features, acts);
        float prediction = acts.back().empty() ? 0.0f : acts.back()[0];
        float diff = prediction - normReward;
        m_lastLoss = diff * diff;
        m_avgLoss = m_avgLoss * 0.9f + m_lastLoss * 0.1f;

        for (int l = 0; l < m_policy.layerCount(); ++l) {
            if (m_policy.layerType(l) == Policy::LayerType::LSTM) continue;
            const int inl  = m_policy.layerInSize(l);
            const int outl = m_policy.layerOutSize(l);
            const auto& input_act = acts[l];
            auto w = m_policy.layerWeights(l);
            for (int o = 0; o < outl; ++o)
                for (int i = 0; i < inl; ++i)
                    w[o * inl + i] -= lr * diff * input_act[i];
            m_policy.setLayerWeights(l, w);
        }
    }
}

// ─── Persistence ─────────────────────────────────────────────────────────────
// Save format per layer:  type in out\n  [weights...]\n  [biases...]\n
// type: 0=DENSE, 1=LSTM.  LSTM weight count = 4*(in+out)*out; bias = 4*out.

bool Trainer::save(const std::string& path) const {
    std::ofstream out(path);
    if (!out) return false;
    out << m_observations << "\n";
    out << m_avgLoss << " " << m_maxReward << "\n";
    for (int l = 0; l < m_policy.layerCount(); ++l) {
        const auto& w = m_policy.layerWeights(l);
        const auto& b = m_policy.layerBiases(l);
        out << (int)m_policy.layerType(l) << " "
            << m_policy.layerInSize(l)    << " "
            << m_policy.layerOutSize(l)   << "\n";
        for (float v : w) out << v << " ";
        out << "\n";
        for (float v : b) out << v << " ";
        out << "\n";
    }
    return out.good();
}

void Trainer::reset() {
    m_observations = 0;
    m_avgLoss = 0.0f;
    m_lastLoss = 0.0f;
    m_maxReward = 1.0f;
    m_replayBuffer.clear();
    m_lastUsedSequence = false;
}


bool Trainer::load(const std::string& path) {
    m_lastUsedSequence = false;
    std::ifstream in(path);
    if (!in) return false;
    if (!(in >> m_observations)) return false;
    in >> m_avgLoss >> m_maxReward;
    for (int l = 0; l < m_policy.layerCount(); ++l) {
        int type = 0, ins = 0, outs = 0;
        if (!(in >> type >> ins >> outs)) return false;
        if (ins  != m_policy.layerInSize(l))  return false;
        if (outs != m_policy.layerOutSize(l)) return false;
        if (type != (int)m_policy.layerType(l)) return false;
        // LSTM weight tensor is 4*(in+out)*out; dense is in*out
        int wcount = (type == (int)Policy::LayerType::LSTM)
                     ? 4 * (ins + outs) * outs
                     : ins * outs;
        int bcount = (type == (int)Policy::LayerType::LSTM)
                     ? 4 * outs
                     : outs;
        std::vector<float> w(wcount), b(bcount);
        for (float& v : w) if (!(in >> v)) return false;
        for (float& v : b) if (!(in >> v)) return false;
        m_policy.setLayerWeights(l, w);
        m_policy.setLayerBiases(l, b);
    }
    return true;
}

