#include <catch2/catch_test_macros.hpp>
#include "nn/loss.h"
#include "nn/advisor.h"

TEST_CASE("Loss computation returns negative generation", "[loss]") {
    TelemetryEntry e;
    e.generation = 0;
    REQUIRE(Loss::compute(e) == 0.0f);
    e.generation = 5;
    REQUIRE(Loss::compute(e) == -5.0f);
}
