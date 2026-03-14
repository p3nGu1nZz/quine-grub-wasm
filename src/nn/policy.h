#pragma once

#include <vector>
#include <cmath>

// Simple feed-forward neural network policy.  Layers are defined by
// their weight matrices and biases.  This is *not* a production ML
// library; it only provides the minimal operations we need for on‑device
// learning.  Supports Dense (fully-connected) and LSTM layer types.

class Policy {
public:
    enum class LayerType { DENSE, LSTM };

    Policy() = default;

    // add a dense layer with given input/output sizes; weights are zeroed
    void addDense(int inSize, int outSize);

    // add an LSTM layer; weights are Xavier-initialised; hidden/cell state
    // starts at zero and persists across forward() calls
    void addLSTM(int inSize, int hiddenSize);

    // run a forward pass on `input` and return the output vector;
    // LSTM hidden/cell state is updated in place (mutable)
    std::vector<float> forward(const std::vector<float>& input) const;

    // forward pass that also records every layer's input activation;
    // activations[0] = input, activations[l+1] = output of layer l
    void forwardActivations(const std::vector<float>& input,
                            std::vector<std::vector<float>>& activations) const;

    // simple ReLU activation applied in-place
    static void relu(std::vector<float>& v);

    // helpers for testing / save-load: set/get layer weights and biases
    void setLayerWeights(int idx, const std::vector<float>& w);
    void setLayerBiases(int idx, const std::vector<float>& b);

    // read-only accessors for architecture inspection / training
    int       layerCount()             const { return (int)m_layers.size(); }
    int       layerInSize(int i)       const { return (i >= 0 && i < (int)m_layers.size()) ? m_layers[i].in  : 0; }
    int       layerOutSize(int i)      const { return (i >= 0 && i < (int)m_layers.size()) ? m_layers[i].out : 0; }
    LayerType layerType(int i)         const { return (i >= 0 && i < (int)m_layers.size()) ? m_layers[i].type : LayerType::DENSE; }
    const std::vector<float>& layerWeights(int i) const {
        static const std::vector<float> empty;
        return (i >= 0 && i < (int)m_layers.size()) ? m_layers[i].weights : empty;
    }
    const std::vector<float>& layerBiases(int i) const {
        static const std::vector<float> empty;
        return (i >= 0 && i < (int)m_layers.size()) ? m_layers[i].biases : empty;
    }

    // reset the mutable state (hidden & cell) of all LSTM layers to zero.
    // Call this before processing a new sequence of inputs.
    void resetState();

    // Run a sequence of inputs through the network.  The internal LSTM
    // state is cleared before the first element is processed.  Returns the
    // final output vector (same as calling `forward()` on the last input)
    std::vector<float> forwardSequence(const std::vector<std::vector<float>>& seq);

private:
    // Layer: Dense or LSTM.
    //   Dense  – weights (out×in), biases (out).
    //   LSTM   – 4 gates, each (in+hidden)→hidden; combined weight tensor
    //            shape [4, hidden, in+hidden]; biases [4, hidden].
    //            Gate order: forget(0), input(1), cell(2), output(3).
    struct Layer {
        LayerType           type    = LayerType::DENSE;
        std::vector<float>  weights;
        std::vector<float>  biases;
        int in  = 0;
        int out = 0;
        // LSTM temporal state – mutable so const forward() can update it
        mutable std::vector<float> lstmH;  // hidden state  (size = out)
        mutable std::vector<float> lstmC;  // cell state    (size = out)
    };
    std::vector<Layer> m_layers;

    // apply one LSTM layer step; updates layer.lstmH/lstmC and current
    void applyLSTM(const Layer& layer, std::vector<float>& current) const;
};
