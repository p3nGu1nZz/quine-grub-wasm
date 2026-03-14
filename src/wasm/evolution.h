#pragma once

#include "wasm/parser.h"
#include "cli.h"  // for MutationStrategy (search path includes src/core)
#include <string>
#include <vector>
#include <stdexcept>
#include <cstring> // for memcpy

enum class EvolutionAction { MODIFY = 0, INSERT = 1, ADD = 2, DELETE = 3 };

// Exception type thrown when evolution fails.  The `binary` field holds the
// base64-encoded candidate that provoked the error (if available) which makes
// debugging invalid mutations much easier.
struct EvolutionException : public std::runtime_error {
    std::string binary;
    EvolutionException(const std::string& msg, const std::string& bin = "")
        : std::runtime_error(msg), binary(bin) {}
};

struct EvolutionResult {
    std::string     binary;           // base64-encoded evolved binary
    std::vector<uint8_t> mutationSequence; // empty = no mutation tracked
    EvolutionAction actionUsed;
    std::string     description;

    // optional weights reported by kernels that implement the
    // `env.record_weight` import.  Each callback invocation appends two
    // floats (encoded as raw bits) to this vector; sequence-model kernels
    // currently send exactly two values per `run`.
    std::vector<float> weightFeedback;
};

// Produce an evolved WASM binary from the current base64-encoded kernel.
// knownInstructions: previously seen instruction byte sequences for guided mutation.
// attemptSeed: determines which action to try (cycles through 0-3).
EvolutionResult evolveBinary(
    const std::string&                             currentBase64,
    const std::vector<std::vector<uint8_t>>&       knownInstructions,
    int                                            attemptSeed,
    MutationStrategy                               strategy = MutationStrategy::RANDOM
);
