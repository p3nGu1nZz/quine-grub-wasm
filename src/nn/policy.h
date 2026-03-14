#pragma once

#include <vector>
#include <cmath>

// Feed-forward neural network policy with Dense and LSTM layers.
//
// Forward pass:   forwardActivations() — records per-layer activations
// Backward pass:  backward()           — full backprop with chain rule
//
// Dense layers use ReLU activation and Xavier-uniform weight init.
// LSTM uses the standard forget/input/cell/output gate formulation.
// Single-step BPTT is implemented for LSTM (no truncated history).

class Policy {
public:
    enum class LayerType { DENSE, LSTM };

    Policy() = default;

    // Add a fully-connected layer (Xavier-uniform init, ReLU activation).
    void addDense(int inSize, int outSize);

    // Add an LSTM layer (Xavier-uniform init).  Hidden/cell state starts
    // at zero and is updated in-place across forward() calls.
    void addLSTM(int inSize, int hiddenSize);

    // Forward pass — returns final output vector.
    std::vector<float> forward(const std::vector<float>& input) const;

    // Forward pass that records every layer's post-activation output.
    // activations[0] = raw input, activations[l+1] = output of layer l.
    void forwardActivations(const std::vector<float>& input,
                            std::vector<std::vector<float>>& activations) const;

    // Backward pass (SGD with gradient clipping and L2 weight decay).
    //
    // acts       — activations from the immediately preceding forwardActivations()
    // outputGrad — dL/dy for the scalar output: 2*(prediction - target) for MSE
    // lr         — learning rate
    // gradClip   — per-element gradient clip magnitude
    // weightDecay— L2 coefficient applied after each weight step
    //
    // All dense layers (including layers before the LSTM) receive proper
    // chain-rule gradients.  The LSTM receives single-step BPTT gradients.
    void backward(const std::vector<std::vector<float>>& acts,
                  float outputGrad,
                  float lr, float gradClip, float weightDecay);

    // ReLU applied in-place.
    static void relu(std::vector<float>& v);

    void setLayerWeights(int idx, const std::vector<float>& w);
    void setLayerBiases (int idx, const std::vector<float>& b);

    int       layerCount()   const { return (int)m_layers.size(); }
    int       layerInSize (int i) const { return valid(i) ? m_layers[i].in  : 0; }
    int       layerOutSize(int i) const { return valid(i) ? m_layers[i].out : 0; }
    LayerType layerType   (int i) const { return valid(i) ? m_layers[i].type : LayerType::DENSE; }

    const std::vector<float>& layerWeights(int i) const {
        static const std::vector<float> empty;
        return valid(i) ? m_layers[i].weights : empty;
    }
    const std::vector<float>& layerBiases(int i) const {
        static const std::vector<float> empty;
        return valid(i) ? m_layers[i].biases : empty;
    }

    // LSTM state accessors (for checkpoint save/load)
    const std::vector<float>& layerLstmH(int i) const {
        static const std::vector<float> empty;
        return (valid(i) && m_layers[i].type == LayerType::LSTM) ? m_layers[i].lstmH : empty;
    }
    const std::vector<float>& layerLstmC(int i) const {
        static const std::vector<float> empty;
        return (valid(i) && m_layers[i].type == LayerType::LSTM) ? m_layers[i].lstmC : empty;
    }
    void setLayerLstmH(int i, const std::vector<float>& h) {
        if (valid(i) && m_layers[i].type == LayerType::LSTM &&
            h.size() == m_layers[i].lstmH.size())
            m_layers[i].lstmH = h;
    }
    void setLayerLstmC(int i, const std::vector<float>& c) {
        if (valid(i) && m_layers[i].type == LayerType::LSTM &&
            c.size() == m_layers[i].lstmC.size())
            m_layers[i].lstmC = c;
    }

    // Zero out LSTM hidden/cell states.  Call before a new sequence.
    void resetState();

    // Feed a full sequence; resets state first, returns final output.
    std::vector<float> forwardSequence(const std::vector<std::vector<float>>& seq);

private:
    bool valid(int i) const { return i >= 0 && i < (int)m_layers.size(); }

    // ── Per-step cache for LSTM backprop ──────────────────────────────────────
    // Saved during applyLSTM(); consumed by backward().
    struct LSTMCache {
        std::vector<float> xh;     // concatenated [input; h_{t-1}]
        std::vector<float> f;      // forget gate activations (post-sigmoid)
        std::vector<float> i_gate; // input  gate activations (post-sigmoid)
        std::vector<float> g;      // cell candidate            (post-tanh)
        std::vector<float> o;      // output gate activations (post-sigmoid)
        std::vector<float> c_prev; // cell state before this step
        bool valid = false;
    };

    // ── Layer ─────────────────────────────────────────────────────────────────
    //   Dense : weights (out×in), biases (out).
    //   LSTM  : weights [4, hidden, in+hidden], biases [4, hidden].
    //           Gate order: forget(0), input(1), cell-candidate(2), output(3).
    struct Layer {
        LayerType          type    = LayerType::DENSE;
        std::vector<float> weights;
        std::vector<float> biases;
        int in  = 0;
        int out = 0;
        // LSTM temporal state (mutable — updated inside const forward())
        mutable std::vector<float> lstmH;
        mutable std::vector<float> lstmC;
        mutable LSTMCache           lstmCache;
    };

    std::vector<Layer> m_layers;

    // One LSTM forward step; saves cache for backprop, updates lstmH/lstmC.
    void applyLSTM(const Layer& layer, std::vector<float>& current) const;
};
