#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;
#include "nn/policy.h"

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
