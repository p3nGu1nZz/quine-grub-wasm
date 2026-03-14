#include "wasm/parser.h"
#include <cstdio>

std::string getOpcodeName(uint8_t byte) {
    switch (byte) {
        case 0x00: return "unreachable";
        case 0x01: return "nop";
        case 0x0B: return "end";
        case 0x1A: return "drop";
        case 0x20: return "local.get";
        case 0x21: return "local.set";
        case 0x22: return "local.tee";
        case 0x41: return "i32.const";
        case 0x10: return "call";
        default: {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "0x%02X", (unsigned)byte);
            return std::string(buf);
        }
    }
}

LEB128Result decodeLEB128(const uint8_t* bytes, size_t size, int offset) {
    uint32_t result = 0;
    int      shift  = 0;
    int      count  = 0;
    while (true) {
        if (offset + count >= (int)size) break;
        uint8_t byte = bytes[offset + count];
        result |= (uint32_t)(byte & 0x7F) << shift;
        shift += 7;
        count++;
        if ((byte & 0x80) == 0) break;
    }
    if (count == 0 && offset < (int)size) return { bytes[offset], 1 };
    if (count == 0)                       return { 0, 0 };
    return { result, count };
}

LEB128Encoded encodeLEB128(uint32_t value) {
    LEB128Encoded enc;
    enc.length = 0;
    if (value == 0) {
        enc.data[0] = 0;
        enc.length = 1;
        return enc;
    }
    while (value > 0 && enc.length < 5) {
        uint8_t byte = (uint8_t)(value & 0x7F);
        value >>= 7;
        if (value != 0) byte |= 0x80;
        enc.data[enc.length++] = byte;
    }
    return enc;
}

// Helper: compute the byte length of an instruction starting at data[ptr]
// without allocating any memory.  Returns {instrLen, argLen}.
static inline void computeInstrLen(const uint8_t* data, size_t len, int ptr,
                                   int& instrLen, int& argLen) {
    uint8_t opcode = data[ptr];
    instrLen = 1;
    argLen = 0;
    if (opcode == 0x41 || opcode == 0x20 || opcode == 0x21 ||
        opcode == 0x22 || opcode == 0x10) {
        auto leb = decodeLEB128(data, len, ptr + 1);
        argLen = leb.length;
        instrLen += argLen;
    } else if (opcode == 0x04) {
        instrLen = 2;
        argLen = 1;
    }
    if (ptr + instrLen > (int)len) {
        instrLen = (int)len - ptr;
        argLen = instrLen > 1 ? instrLen - 1 : 0;
    }
}

std::vector<Instruction> parseInstructions(const uint8_t* data, size_t len) {
    std::vector<Instruction> instructions;
    instructions.reserve(len / 2); // heuristic: avg ~ 2 bytes per instr
    int ptr = 0;
    while (ptr < (int)len) {
        Instruction inst;
        inst.opcode = data[ptr];
        inst.originalOffset = ptr;

        int instrLen, argLen;
        computeInstrLen(data, len, ptr, instrLen, argLen);

        inst.length = instrLen;
        inst.argLen = (uint8_t)(argLen < kMaxInstrArgs ? argLen : kMaxInstrArgs);
        if (inst.argLen > 0) {
            std::memcpy(inst.args, data + ptr + 1, inst.argLen);
        }

        instructions.push_back(inst);
        ptr += instrLen;
    }
    return instructions;
}

std::vector<uint8_t> extractOpcodes(const uint8_t* data, size_t len) {
    std::vector<uint8_t> opcodes;
    opcodes.reserve(len / 2);
    int ptr = 0;
    while (ptr < (int)len) {
        opcodes.push_back(data[ptr]);
        int instrLen, argLen;
        computeInstrLen(data, len, ptr, instrLen, argLen);
        ptr += instrLen;
    }
    return opcodes;
}

// Helper: navigate the WASM binary to find the first function body's
// instruction range.  Returns {instructionStart, endOpIndex} or {-1,-1}.
static std::pair<int,int> locateCodeBody(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 8) return {-1, -1};

    int ptr = 8;
    int codeSectionContentStart = -1;

    while (ptr < (int)bytes.size()) {
        uint8_t id       = bytes[ptr];
        auto    sizeData = decodeLEB128(bytes.data(), bytes.size(), ptr + 1);
        if (id == 10) {
            codeSectionContentStart = ptr + 1 + sizeData.length;
            break;
        }
        ptr = ptr + 1 + sizeData.length + (int)sizeData.value;
    }

    if (codeSectionContentStart == -1) return {-1, -1};

    auto numFuncsData    = decodeLEB128(bytes.data(), bytes.size(), codeSectionContentStart);
    int  funcBodySizeOff = codeSectionContentStart + numFuncsData.length;
    auto funcBodySizeData = decodeLEB128(bytes.data(), bytes.size(), funcBodySizeOff);
    int  funcContentStart = funcBodySizeOff + funcBodySizeData.length;

    auto localCountData = decodeLEB128(bytes.data(), bytes.size(), funcContentStart);
    int  instrPtr       = funcContentStart + localCountData.length;

    for (int i = 0; i < (int)localCountData.value; i++) {
        auto countData = decodeLEB128(bytes.data(), bytes.size(), instrPtr);
        instrPtr += countData.length + 1;
    }

    int instructionStart = instrPtr;
    int funcEnd          = funcContentStart + (int)funcBodySizeData.value;
    int endOpIndex       = funcEnd - 1;

    if (instructionStart >= endOpIndex) return {-1, -1};
    return {instructionStart, endOpIndex};
}

std::vector<Instruction> extractCodeSection(const std::vector<uint8_t>& bytes) {
    auto [start, end] = locateCodeBody(bytes);
    if (start < 0) return {};
    return parseInstructions(bytes.data() + start, (size_t)(end - start));
}

std::vector<uint8_t> extractCodeSectionOpcodes(const std::vector<uint8_t>& bytes) {
    auto [start, end] = locateCodeBody(bytes);
    if (start < 0) return {};
    return extractOpcodes(bytes.data() + start, (size_t)(end - start));
}
