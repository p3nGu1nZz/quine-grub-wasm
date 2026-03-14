#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>

#include "base64.h"
#include <vector>
#include <string>

static std::string toString(const std::vector<uint8_t>& v) {
    return std::string(v.begin(), v.end());
}

TEST_CASE("base64 encode/decode round trip", "[base64]") {
    std::string hello = "The quick brown fox jumps over the lazy dog";
    std::vector<uint8_t> data(hello.begin(), hello.end());
    std::string encoded = base64_encode(data);
    REQUIRE(!encoded.empty());
    auto decoded = base64_decode(encoded);
    REQUIRE(toString(decoded) == hello);
}

TEST_CASE("base64 decode known string", "[base64]") {
    // "Hello" -> SGVsbG8=
    auto decoded = base64_decode("SGVsbG8=");
    REQUIRE(toString(decoded) == "Hello");
}

TEST_CASE("base64 decode ignores invalid chars/whitespace", "[base64]") {
    // insert some spaces and invalid characters
    auto decoded = base64_decode("SGV s bG8=!!");
    REQUIRE(toString(decoded) == "Hello");
}

TEST_CASE("base64 decode handles padding properly", "[base64]") {
    auto d1 = base64_decode("YQ=="); // "a"
    REQUIRE(toString(d1) == "a");
    auto d2 = base64_decode("YWI="); // "ab"
    REQUIRE(toString(d2) == "ab");
}
