#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>

#include "util.h"
#include "types.h"

TEST_CASE("stateStr returns correct string", "[util]") {
    REQUIRE(stateStr(SystemState::IDLE) == "IDLE");
}
