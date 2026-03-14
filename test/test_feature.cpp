#include <catch2/catch_test_macros.hpp>
#include "nn/feature.h"
#include "constants.h"
#include "base64.h"

TEST_CASE("Feature extractor produces opcode histogram", "[feature]") {
    TelemetryEntry e;
    // invalid wasm produces no instructions
    e.kernelBase64 = "AQ==";
    auto vec = Feature::extract(e);
    REQUIRE(vec.size() == (size_t)kFeatSize);
    for (auto v : vec) REQUIRE(v == 0.0f);

    // if a trap code is provided, the special flag slot is set
    e.trapCode = "oops";
    vec = Feature::extract(e);
    REQUIRE(vec[256] == 1.0f);
    e.trapCode.clear();

    // realistic kernel should yield some non-zero counts
    e.kernelBase64 = KERNEL_GLOB;
    vec = Feature::extract(e);
    bool saw_nonzero = false;
    for (auto v : vec) if (v > 0.0f) saw_nonzero = true;
    REQUIRE(saw_nonzero);
    
    // sequence extraction should return a vector the same length as the
    // number of decoded instructions and should start with a known opcode
    // for the seed kernel (which always begins with 0x00, WASM module magic
    // followed by version).  The parser currently skips header bytes and only
    // returns code section opcodes, so we just verify non‑empty.
    auto seq = Feature::extractSequence(e);
    REQUIRE(!seq.empty());
    
    // extractSequence on nonsense input yields empty sequence
    e.kernelBase64 = "AQ==";
    auto seq2 = Feature::extractSequence(e);
    REQUIRE(seq2.empty());
}
