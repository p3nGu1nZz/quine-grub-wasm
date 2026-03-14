#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;
#include "nn/policy.h"
#include <cmath>
#include <numeric>

// ─── Dense forward pass ───────────────────────────────────────────────────────

TEST_CASE("Policy forward pass and layer addition", "[policy]") {
    Policy p;
    p.addDense(3, 2); // one layer 3->2
    // configure weights so output = [in0+in1, in1+in2]
    std::vector<float> weights = {
        1.0f, 1.0f, 0.0f, // first output neuron
        0.0f, 1.0f, 1.0f  // second output neuron
    };
    std::vector<float> biases = {0.0f, 0.0f};
    p.setLayerWeights(0, weights);
    p.setLayerBiases(0, biases);

    std::vector<float> input = {1.0f, 2.0f, 3.0f};
    auto out = p.forward(input);
    REQUIRE(out.size() == 2);
    REQUIRE(out[0] == Approx(3.0f)); // 1+2
    REQUIRE(out[1] == Approx(5.0f)); // 2+3
}

// ─── Xavier initialisation ────────────────────────────────────────────────────

TEST_CASE("addDense initialises weights with Xavier-uniform (non-zero)", "[policy][init]") {
    Policy p;
    p.addDense(16, 8);
    const auto& w = p.layerWeights(0);
    REQUIRE(w.size() == (size_t)(16 * 8));

    // With Xavier init the weights must not all be zero
    bool allZero = true;
    for (float v : w) if (v != 0.0f) { allZero = false; break; }
    REQUIRE(!allZero);

    // Weights must be within the Xavier-uniform bound ±sqrt(6/(16+8))
    float bound = std::sqrt(6.0f / (16 + 8));
    for (float v : w) {
        REQUIRE(v >= -bound - 1e-6f);
        REQUIRE(v <=  bound + 1e-6f);
    }
}

TEST_CASE("addLSTM initialises weights with Xavier-uniform (non-zero)", "[policy][init][lstm]") {
    Policy p;
    p.addLSTM(4, 4);
    const auto& w = p.layerWeights(0);
    REQUIRE(!w.empty());
    bool allZero = true;
    for (float v : w) if (v != 0.0f) { allZero = false; break; }
    REQUIRE(!allZero);
}

// ─── forwardActivations ───────────────────────────────────────────────────────

TEST_CASE("forwardActivations records one activation vector per layer plus input", "[policy][acts]") {
    Policy p;
    p.addDense(4, 3); // layer 0
    p.addDense(3, 2); // layer 1

    std::vector<float> input = {1.0f, 0.5f, -0.5f, 0.25f};
    std::vector<std::vector<float>> acts;
    p.forwardActivations(input, acts);

    // acts[0]=input, acts[1]=out_L0, acts[2]=out_L1
    REQUIRE(acts.size() == 3);
    REQUIRE(acts[0].size() == 4);
    REQUIRE(acts[1].size() == 3);
    REQUIRE(acts[2].size() == 2);
}

TEST_CASE("forwardActivations pads undersized input without crashing", "[policy][acts][safety]") {
    Policy p;
    p.addDense(8, 4);

    std::vector<float> tiny = {1.0f}; // only 1 element, network expects 8
    std::vector<std::vector<float>> acts;
    REQUIRE_NOTHROW(p.forwardActivations(tiny, acts));
    REQUIRE(acts.size() == 2);
    REQUIRE(acts[0].size() == 8); // padded to expected size
}

// ─── backward ────────────────────────────────────────────────────────────────

TEST_CASE("backward changes weights after a single update", "[policy][backprop]") {
    Policy p;
    p.addDense(4, 2);
    p.addDense(2, 1);

    auto w0before = p.layerWeights(0);
    auto w1before = p.layerWeights(1);

    std::vector<float> input = {1.0f, 1.0f, 1.0f, 1.0f};
    std::vector<std::vector<float>> acts;
    p.forwardActivations(input, acts);

    // Scalar output gradient (2*(pred - target), here just use 1.0)
    p.backward(acts, 1.0f, 0.01f, 5.0f, 0.0f);

    auto w0after = p.layerWeights(0);
    auto w1after = p.layerWeights(1);

    // At least one weight must have changed in each layer
    bool w0changed = false, w1changed = false;
    for (size_t i = 0; i < w0before.size(); ++i)
        if (w0before[i] != w0after[i]) { w0changed = true; break; }
    for (size_t i = 0; i < w1before.size(); ++i)
        if (w1before[i] != w1after[i]) { w1changed = true; break; }

    REQUIRE(w0changed);
    REQUIRE(w1changed);
}

TEST_CASE("backward gradient is clipped to gradClip magnitude", "[policy][backprop][clip]") {
    Policy p;
    p.addDense(2, 2);
    p.addDense(2, 1);

    // Manually set large weights so gradient is large before clipping
    p.setLayerWeights(0, {100.0f, 100.0f, 100.0f, 100.0f});
    p.setLayerWeights(1, {100.0f, 100.0f});
    p.setLayerBiases(0, {0.0f, 0.0f});
    p.setLayerBiases(1, {0.0f});

    auto w0before = p.layerWeights(0);

    std::vector<float> input = {1.0f, 1.0f};
    std::vector<std::vector<float>> acts;
    p.forwardActivations(input, acts);

    const float clip = 0.1f;
    const float lr   = 1.0f; // use lr=1 so the weight change equals the clipped grad
    p.backward(acts, 1e6f, lr, clip, 0.0f);

    auto w0after = p.layerWeights(0);

    // Each weight change must be at most clip * lr in magnitude
    for (size_t i = 0; i < w0before.size(); ++i) {
        float delta = std::abs(w0after[i] - w0before[i]);
        REQUIRE(delta <= clip * lr + 1e-5f);
    }
}

TEST_CASE("backward with LSTM: weights change for all layer types", "[policy][backprop][lstm]") {
    Policy p;
    p.addDense(4, 2);
    p.addLSTM(2, 2);
    p.addDense(2, 1);

    auto w0before = p.layerWeights(0);
    auto wLbefore = p.layerWeights(1);
    auto w2before = p.layerWeights(2);

    std::vector<float> input = {0.5f, 0.5f, 0.5f, 0.5f};
    std::vector<std::vector<float>> acts;
    p.forwardActivations(input, acts);
    p.backward(acts, 0.5f, 0.01f, 5.0f, 0.0f);

    auto w0after = p.layerWeights(0);
    auto wLafter = p.layerWeights(1);
    auto w2after = p.layerWeights(2);

    bool dense0Changed = false, lstmChanged = false, dense2Changed = false;
    for (size_t i = 0; i < w0before.size(); ++i)
        if (w0before[i] != w0after[i]) { dense0Changed = true; break; }
    for (size_t i = 0; i < wLbefore.size(); ++i)
        if (wLbefore[i] != wLafter[i]) { lstmChanged = true; break; }
    for (size_t i = 0; i < w2before.size(); ++i)
        if (w2before[i] != w2after[i]) { dense2Changed = true; break; }

    REQUIRE(dense0Changed);
    REQUIRE(lstmChanged);
    REQUIRE(dense2Changed);
}

// ─── LSTM state ───────────────────────────────────────────────────────────────

TEST_CASE("LSTM state persists between forward calls and can be reset", "[policy][lstm]") {
    Policy p;
    p.addLSTM(1, 1);

    std::vector<float> u = {1.0f};
    // run twice without resetting; output should change
    auto o1 = p.forward(u);
    auto o2 = p.forward(u);
    REQUIRE(o2 != o1); // hidden state has changed, so outputs differ

    // reset and run again; the first output should match the original first
    // output, but the second may differ because the hidden state advances.
    p.resetState();
    auto r1 = p.forward(u);
    auto r2 = p.forward(u);
    REQUIRE(r1.size() == o1.size());
    REQUIRE(r1[0] == Approx(o1[0]));
    REQUIRE(r2.size() == r1.size());
    REQUIRE(r2[0] != Approx(r1[0]));
}

TEST_CASE("forwardSequence is equivalent to manual sequential forwards", "[policy][lstm]") {
    Policy p;
    p.addLSTM(1, 1);

    std::vector<float> u = {1.0f};
    p.resetState();
    auto m = p.forward(u);
    m = p.forward(u);

    p.resetState();
    auto seq = p.forwardSequence({u, u});
    REQUIRE(seq.size() == m.size());
    REQUIRE(seq == m);
}

// ─── LSTM state accessors ─────────────────────────────────────────────────────

TEST_CASE("setLayerLstmH / setLayerLstmC roundtrip", "[policy][lstm][state]") {
    Policy p;
    p.addLSTM(2, 2);
    p.forward({0.5f, 0.5f}); // advance hidden state

    auto h = p.layerLstmH(0);
    auto c = p.layerLstmC(0);
    REQUIRE(h.size() == 2);
    REQUIRE(c.size() == 2);

    // Overwrite with known values
    std::vector<float> newH = {0.1f, 0.2f};
    std::vector<float> newC = {0.3f, 0.4f};
    p.setLayerLstmH(0, newH);
    p.setLayerLstmC(0, newC);

    REQUIRE(p.layerLstmH(0)[0] == Approx(0.1f));
    REQUIRE(p.layerLstmH(0)[1] == Approx(0.2f));
    REQUIRE(p.layerLstmC(0)[0] == Approx(0.3f));
    REQUIRE(p.layerLstmC(0)[1] == Approx(0.4f));

    // After explicit state restore, forward output should be deterministic
    auto out1 = p.forward({1.0f, 0.0f});
    p.setLayerLstmH(0, newH);
    p.setLayerLstmC(0, newC);
    auto out2 = p.forward({1.0f, 0.0f});
    REQUIRE(out1[0] == Approx(out2[0]));
}

// ─── Multi-step training drives output toward target ─────────────────────────

TEST_CASE("Repeated backward calls reduce prediction error on a fixed sample", "[policy][backprop][convergence]") {
    // Small network: 2 → 4 → 1
    Policy p;
    p.addDense(2, 4);
    p.addDense(4, 1);

    std::vector<float> input  = {1.0f, 0.5f};
    const float target = 0.8f;

    float initPred = p.forward(input)[0];
    float prevErr  = std::abs(initPred - target);

    for (int step = 0; step < 200; ++step) {
        std::vector<std::vector<float>> acts;
        p.forwardActivations(input, acts);
        float pred  = acts.back()[0];
        float grad  = 2.0f * (pred - target);
        p.backward(acts, grad, 0.05f, 5.0f, 0.0f);
    }

    float finalPred = p.forward(input)[0];
    float finalErr  = std::abs(finalPred - target);
    // Error should have reduced (not necessarily to zero, but meaningfully)
    REQUIRE(finalErr < prevErr);
}
