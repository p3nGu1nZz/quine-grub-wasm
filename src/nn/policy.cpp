#include "nn/policy.h"
#include <algorithm>
#include <cstdint>

void Policy::addDense(int inSize, int outSize) {
    Layer layer;
    layer.type = LayerType::DENSE;
    layer.in = inSize;
    layer.out = outSize;
    layer.weights.assign(inSize * outSize, 0.0f);
    layer.biases.assign(outSize, 0.0f);
    m_layers.push_back(std::move(layer));
}

void Policy::addLSTM(int inSize, int hiddenSize) {
    Layer layer;
    layer.type = LayerType::LSTM;
    layer.in  = inSize;
    layer.out = hiddenSize;
    // Weight tensor: 4 gates × hiddenSize × (inSize + hiddenSize)
    // Bias tensor:   4 gates × hiddenSize
    int total_in = inSize + hiddenSize;
    layer.weights.resize(4 * hiddenSize * total_in);
    layer.biases.assign(4 * hiddenSize, 0.0f);
    // Xavier-uniform initialisation: range ±sqrt(6/(in+out))
    float bound = std::sqrt(6.0f / static_cast<float>(inSize + hiddenSize));
    uint32_t lcg = 0x9e3779b9u;
    for (auto& w : layer.weights) {
        lcg = lcg * 1664525u + 1013904223u;
        float r = (static_cast<int32_t>(lcg) / 2147483648.0f); // [-1, 1]
        w = r * bound;
    }
    layer.lstmH.assign(hiddenSize, 0.0f);
    layer.lstmC.assign(hiddenSize, 0.0f);
    m_layers.push_back(std::move(layer));
}

// ─── LSTM step ───────────────────────────────────────────────────────────────

void Policy::applyLSTM(const Layer& layer, std::vector<float>& current) const {
    int hidden   = layer.out;
    int total_in = layer.in + hidden;

    // Concatenate input with previous hidden state: xh = [current; lstmH]
    std::vector<float> xh(current);
    xh.insert(xh.end(), layer.lstmH.begin(), layer.lstmH.end());

    // Compute raw gate activations: 4 × hidden
    std::vector<float> gates(4 * hidden, 0.0f);
    for (int g = 0; g < 4; ++g) {
        int woff = g * hidden * total_in;
        int boff = g * hidden;
        for (int h = 0; h < hidden; ++h) {
            float sum = layer.biases[boff + h];
            for (int x = 0; x < total_in; ++x)
                sum += layer.weights[woff + h * total_in + x] * xh[x];
            gates[g * hidden + h] = sum;
        }
    }

    // Apply activations: gates 0,1,3 = sigmoid; gate 2 (cell) = tanh
    for (int g = 0; g < 4; ++g) {
        for (int h = 0; h < hidden; ++h) {
            float& v = gates[g * hidden + h];
            if (g == 2)
                v = std::tanh(v);
            else
                v = 1.0f / (1.0f + std::exp(-v));
        }
    }

    // Update cell and hidden states
    // c = forget * c_prev + input * cell_candidate
    // h = output * tanh(c)
    for (int h = 0; h < hidden; ++h) {
        float f = gates[0 * hidden + h]; // forget
        float i = gates[1 * hidden + h]; // input
        float g = gates[2 * hidden + h]; // cell candidate
        float o = gates[3 * hidden + h]; // output
        layer.lstmC[h] = f * layer.lstmC[h] + i * g;
        layer.lstmH[h] = o * std::tanh(layer.lstmC[h]);
    }
    current = layer.lstmH;
}

// ─── Forward pass ────────────────────────────────────────────────────────────

void Policy::forwardActivations(const std::vector<float>& input,
                                std::vector<std::vector<float>>& activations) const {
    activations.clear();
    activations.push_back(input);          // activations[0] = raw input
    std::vector<float> current = input;
    for (const auto& layer : m_layers) {
        if (layer.type == LayerType::DENSE) {
            std::vector<float> next(layer.out, 0.0f);
            for (int o = 0; o < layer.out; ++o) {
                float sum = layer.biases[o];
                for (int i = 0; i < layer.in; ++i)
                    sum += layer.weights[o * layer.in + i] * current[i];
                next[o] = sum;
            }
            relu(next);
            current.swap(next);
        } else {
            applyLSTM(layer, current);
        }
        activations.push_back(current);    // activations[l+1] = output of layer l
    }
}

std::vector<float> Policy::forward(const std::vector<float>& input) const {
    std::vector<std::vector<float>> acts;
    forwardActivations(input, acts);
    return acts.empty() ? input : acts.back();
}

void Policy::resetState() {
    for (auto &layer : m_layers) {
        if (layer.type == LayerType::LSTM) {
            layer.lstmH.assign(layer.out, 0.0f);
            layer.lstmC.assign(layer.out, 0.0f);
        }
    }
}

std::vector<float> Policy::forwardSequence(const std::vector<std::vector<float>>& seq) {
    resetState();
    std::vector<float> out;
    for (const auto& v : seq) {
        out = forward(v);
    }
    return out;
}

void Policy::relu(std::vector<float>& v) {
    for (auto& x : v) if (x < 0.0f) x = 0.0f;
}

// ─── Layer weight/bias setters ────────────────────────────────────────────────

void Policy::setLayerWeights(int idx, const std::vector<float>& w) {
    if (idx < 0 || idx >= (int)m_layers.size()) return;
    auto& layer = m_layers[idx];
    if ((int)w.size() == (int)layer.weights.size())
        layer.weights = w;
}

void Policy::setLayerBiases(int idx, const std::vector<float>& b) {
    if (idx < 0 || idx >= (int)m_layers.size()) return;
    auto& layer = m_layers[idx];
    if ((int)b.size() == (int)layer.biases.size())
        layer.biases = b;
}
