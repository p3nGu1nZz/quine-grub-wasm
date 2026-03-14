#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <optional>
#include <cstring>

// Maximum argument bytes any single WASM instruction can carry.
// LEB128 values are at most 5 bytes; the `if` opcode has 1 byte.
static constexpr int kMaxInstrArgs = 5;

struct Instruction {
    uint8_t  opcode;
    uint8_t  argLen;                 // number of valid bytes in args[]
    uint8_t  args[kMaxInstrArgs];    // inline small buffer – no heap alloc
    int      length;
    int      originalOffset;

    Instruction() : opcode(0), argLen(0), length(0), originalOffset(0) {
        std::memset(args, 0, sizeof(args));
    }
};

std::string getOpcodeName(uint8_t byte);

struct LEB128Result {
    uint32_t value;
    int      length;
};

// Small inline buffer returned by encodeLEB128 – avoids heap allocation.
struct LEB128Encoded {
    uint8_t data[5];
    int     length;
};

LEB128Result  decodeLEB128(const uint8_t* bytes, size_t size, int offset);
LEB128Encoded encodeLEB128(uint32_t value);

std::vector<Instruction> parseInstructions(const uint8_t* data, size_t len);

// Fast path: extract just the opcode bytes from raw WASM instruction
// stream without building full Instruction objects.
std::vector<uint8_t> extractOpcodes(const uint8_t* data, size_t len);

// Returns instructions from the code section of a WASM binary.
// Returns empty vector if code section not found.
std::vector<Instruction> extractCodeSection(const std::vector<uint8_t>& bytes);

// Fast path: extract just opcode bytes from the code section.
std::vector<uint8_t> extractCodeSectionOpcodes(const std::vector<uint8_t>& bytes);
