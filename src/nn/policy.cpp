#include "nn/policy.h"
#include <algorithm>
#include <cstdint>

// Simple LCG for weight initialisation (avoids <random> cost at startup).
static uint32_t lcgNext(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s;
}

static float xavierSample(uint32_t& s, float bound) {
    // uniform [-bound, bound]
    float r = static_cast<float>(static_cast<int32_t>(lcgNext(s))) / 2147483648.0f;
    return r * bound;
}

void Policy::addDense(int inSize, int outSize) {
    Layer layer;
    layer.type = LayerType::DENSE;
    layer.in   = inSize;
    layer.out  = outSize;
    // Xavier-uniform: range ±sqrt(6/(fan_in + fan_out))
    float bound = std::sqrt(6.0f / static_cast<float>(inSize + outSize));
    layer.weights.resize(inSize * outSize);
    uint32_t s = 0x9e3779b9u ^ static_cast<uint32_t>(inSize * 31 + outSize);
    for (auto& w : layer.weights) w = xavierSample(s, bound);
    layer.biases.assign(outSize, 0.0f);
    m_layers.push_back(std::move(layer));
}

void Policy::addLSTM(int inSize, int hiddenSize) {
    Layer layer;
    layer.type = LayerType::LSTM;
    layer.in  = inSize;
    layer.out = hiddenSize;
    int total_in = inSize + hiddenSize;
    layer.weights.resize(4 * hiddenSize * total_in);
    layer.biases.assign(4 * hiddenSize, 0.0f);
    float bound = std::sqrt(6.0f / static_cast<float>(inSize + hiddenSize));
    uint32_t s = 0x9e3779b9u ^ static_cast<uint32_t>(inSize * 31 + hiddenSize * 7);
    for (auto& w : layer.weights) w = xavierSample(s, bound);
    layer.lstmH.assign(hiddenSize, 0.0f);
    layer.lstmC.assign(hiddenSize, 0.0f);
    m_layers.push_back(std::move(layer));
}

// ─── LSTM step ───────────────────────────────────────────────────────────────

void Policy::applyLSTM(const Layer& layer, std::vector<float>& current) const {
    int hidden   = layer.out;
    int total_in = layer.in + hidden;

    // Save for backprop: c_{t-1} and the concatenated [input; h_{t-1}]
    layer.lstmCache.c_prev = layer.lstmC;

    // Concatenate input with previous hidden state: xh = [current; lstmH]
    std::vector<float> xh(current);
    xh.insert(xh.end(), layer.lstmH.begin(), layer.lstmH.end());
    layer.lstmCache.xh = xh;

    // Compute raw gate pre-activations: 4 × hidden
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

    // Apply activations: forget/input/output gates = sigmoid; cell = tanh
    for (int g = 0; g < 4; ++g) {
        for (int h = 0; h < hidden; ++h) {
            float& v = gates[g * hidden + h];
            v = (g == 2) ? std::tanh(v) : 1.0f / (1.0f + std::exp(-v));
        }
    }

    // Save post-activation gate values for backprop
    layer.lstmCache.f.resize(hidden);
    layer.lstmCache.i_gate.resize(hidden);
    layer.lstmCache.g.resize(hidden);
    layer.lstmCache.o.resize(hidden);
    for (int h = 0; h < hidden; ++h) {
        layer.lstmCache.f[h]      = gates[0 * hidden + h]; // forget
        layer.lstmCache.i_gate[h] = gates[1 * hidden + h]; // input
        layer.lstmCache.g[h]      = gates[2 * hidden + h]; // cell candidate
        layer.lstmCache.o[h]      = gates[3 * hidden + h]; // output
    }
    layer.lstmCache.valid = true;

    // Update cell and hidden states:
    //   c_t = f * c_{t-1}  +  i * g
    //   h_t = o * tanh(c_t)
    for (int h = 0; h < hidden; ++h) {
        float f = layer.lstmCache.f[h];
        float i = layer.lstmCache.i_gate[h];
        float g = layer.lstmCache.g[h];
        float o = layer.lstmCache.o[h];
        layer.lstmC[h] = f * layer.lstmC[h] + i * g;
        layer.lstmH[h] = o * std::tanh(layer.lstmC[h]);
    }
    current = layer.lstmH;
}

// ─── Forward pass ────────────────────────────────────────────────────────────

void Policy::forwardActivations(const std::vector<float>& input,
                                std::vector<std::vector<float>>& activations) const {
    activations.clear();
    if (m_layers.empty()) { activations.push_back(input); return; }

    // Ensure the input is exactly the size the first layer expects.
    // Pad with zeros if shorter; truncate if longer (test-harness safety).
    const int expectedIn = m_layers[0].in;
    std::vector<float> safeInput(expectedIn, 0.0f);
    const int copyLen = std::min(static_cast<int>(input.size()), expectedIn);
    std::copy(input.begin(), input.begin() + copyLen, safeInput.begin());

    activations.push_back(safeInput);         // activations[0] = (padded) input
    std::vector<float> current = safeInput;
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

// ─── Backward pass ───────────────────────────────────────────────────────────
//
// Network:  D0(1024→32) → D1(32→64) → LSTM(64→64) → D3(64→32) → D4(32→1)
// acts:     [input, out_D0, out_D1, out_LSTM, out_D3, out_D4]  (all post-ReLU)
//
// Algorithm
// ---------
// 1. Start with outputGrad = dL/dy = 2*(y_pred - target)  [MSE]
// 2. For each dense layer from last to first:
//      a. Apply ReLU derivative mask: delta *= (acts[l+1] > 0)
//      b. Update W[l] -= lr * clip(delta ⊗ acts[l])
//         Update b[l] -= lr * clip(delta)
//         Apply L2 decay: W[l] *= (1 - weightDecay)
//      c. Propagate: delta = W[l]^T @ delta  (for next layer back)
// 3. For the LSTM layer (single-step BPTT):
//      a. Compute gate gradients from dh (delta arriving at LSTM output)
//      b. Update LSTM weights using cached xh, gate values
//      c. Propagate: delta = dxh[0:layer.in]  (gradient to LSTM input)
// 4. Continue with dense layers before the LSTM using the propagated delta.
//
// Note: ReLU derivative uses the post-activation value: ReLU'(z) = (a > 0).

void Policy::backward(const std::vector<std::vector<float>>& acts,
                      float outputGrad,
                      float lr, float gradClip, float weightDecay) {
    const int L = (int)m_layers.size();
    if (L == 0 || acts.size() < static_cast<size_t>(L + 1)) return;

    auto clip = [&](float v) {
        return std::max(-gradClip, std::min(gradClip, v));
    };

    // delta[o] = gradient of loss w.r.t. the OUTPUT of the current layer.
    // Starts as a 1-element vector from the output neuron.
    std::vector<float> delta = { outputGrad };

    for (int l = L - 1; l >= 0; --l) {
        Layer& layer = m_layers[l];

        // ── Dense layer ───────────────────────────────────────────────────────
        if (layer.type == LayerType::DENSE) {
            const int inl  = layer.in;
            const int outl = layer.out;

            // acts[l+1] is the post-ReLU output of this layer.
            // Apply ReLU derivative mask: if output[o] == 0, gate was closed.
            for (int o = 0; o < outl && o < (int)delta.size(); ++o)
                delta[o] *= (acts[l + 1][o] > 0.0f ? 1.0f : 0.0f);

            // Weight and bias gradient descent.
            // dW[o,i] = delta[o] * acts[l][i]  (input activation to this layer)
            const std::vector<float>& in_act = acts[l];
            for (int o = 0; o < outl; ++o) {
                layer.biases[o] -= lr * clip(delta[o]);
                const float d = delta[o];
                for (int i = 0; i < inl; ++i) {
                    float wg = clip(d * in_act[i]);
                    layer.weights[o * inl + i] -= lr * wg;
                    layer.weights[o * inl + i] *= (1.0f - weightDecay);
                }
            }

            // Propagate delta backward: delta_prev[i] = Σ_o W[o,i] * delta[o]
            if (l > 0) {
                std::vector<float> prevDelta(inl, 0.0f);
                for (int i = 0; i < inl; ++i)
                    for (int o = 0; o < outl; ++o)
                        prevDelta[i] += layer.weights[o * inl + i] * delta[o];
                delta = std::move(prevDelta);
            }

        // ── LSTM layer (single-step BPTT) ─────────────────────────────────────
        } else {
            const int hidden   = layer.out;
            const int total_in = layer.in + hidden;
            const LSTMCache& cache = layer.lstmCache;

            if (!cache.valid || (int)delta.size() != hidden) {
                // No cache or dimension mismatch — zero gradient to previous
                delta.assign(layer.in, 0.0f);
                continue;
            }

            // dh = delta (gradient arriving at h_t from the layer above)
            //
            // h_t = o * tanh(c_t)
            //   dL/do    = dh * tanh(c_t)
            //   dL/dc_t  = dh * o * sech²(c_t)
            //
            // c_t = f * c_{t-1} + i * g
            //   dL/df = dc * c_{t-1}
            //   dL/di = dc * g
            //   dL/dg = dc * i
            //
            // Then propagate through gate activations:
            //   df_raw = dL/df * f*(1-f)   (sigmoid derivative)
            //   di_raw = dL/di * i*(1-i)
            //   dg_raw = dL/dg * (1-g^2)  (tanh derivative)
            //   do_raw = dL/do * o*(1-o)

            std::vector<float> df(hidden), di(hidden), dg_cell(hidden), do_(hidden);
            for (int h = 0; h < hidden; ++h) {
                float ct  = layer.lstmC[h];
                float th  = std::tanh(ct);
                float sech2 = 1.0f - th * th;

                float dh_h = delta[h];
                float dc   = dh_h * cache.o[h] * sech2;

                do_[h]     = (dh_h * th)         * cache.o[h]      * (1.0f - cache.o[h]);
                df[h]      = (dc   * cache.c_prev[h]) * cache.f[h]  * (1.0f - cache.f[h]);
                di[h]      = (dc   * cache.g[h])  * cache.i_gate[h] * (1.0f - cache.i_gate[h]);
                dg_cell[h] = (dc   * cache.i_gate[h]) * (1.0f - cache.g[h] * cache.g[h]);
            }

            // Gate gradient array (gate order matches weight layout):
            //   0=forget, 1=input, 2=cell, 3=output
            const std::vector<float>* dGates[4] = { &df, &di, &dg_cell, &do_ };

            // Step 1: compute dxh using ORIGINAL weights (before update)
            std::vector<float> dxh(total_in, 0.0f);
            for (int gi = 0; gi < 4; ++gi) {
                int woff = gi * hidden * total_in;
                for (int h = 0; h < hidden; ++h) {
                    float dval = (*dGates[gi])[h];
                    for (int x = 0; x < total_in; ++x)
                        dxh[x] += layer.weights[woff + h * total_in + x] * dval;
                }
            }

            // Step 2: update LSTM weights and biases
            for (int gi = 0; gi < 4; ++gi) {
                int woff = gi * hidden * total_in;
                int boff = gi * hidden;
                for (int h = 0; h < hidden; ++h) {
                    float d = (*dGates[gi])[h];
                    layer.biases[boff + h] -= lr * clip(d);
                    for (int x = 0; x < total_in; ++x) {
                        float wg = clip(d * cache.xh[x]);
                        layer.weights[woff + h * total_in + x] -= lr * wg;
                        layer.weights[woff + h * total_in + x] *= (1.0f - weightDecay);
                    }
                }
            }

            // Step 3: propagate gradient to LSTM input (output of prev layer)
            // xh = [input; h_{t-1}], so dx = dxh[0:layer.in]
            delta.assign(layer.in, 0.0f);
            for (int x = 0; x < layer.in; ++x)
                delta[x] = dxh[x];
        }
    }
}
