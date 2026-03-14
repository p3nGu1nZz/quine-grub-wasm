#include "wasm/evolution.h"
#include "base64.h"
#include "kernel.h"  // for Validate mutated binaries

#include <cstdlib>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <algorithm>
#include <random>

// Thread-local Mersenne Twister for determinism-free mutations
static std::mt19937& rng() {
    static thread_local std::mt19937 gen(std::random_device{}());
    return gen;
}

static float randF() {
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng());
}

static int randInt(int n) {
    return (int)std::uniform_int_distribution<int>(0, n - 1)(rng());
}

static std::vector<uint8_t> generateRandomConstDrop() {
    return { 0x41, (uint8_t)(randInt(128)), 0x1A };
}

static std::vector<uint8_t> generateSafeMath() {
    static const uint8_t ops[] = { 0x6A, 0x6B, 0x71, 0x72, 0x73 };
    uint8_t op = ops[randInt(5)];
    return {
        0x41, (uint8_t)(randInt(128)),
        0x41, (uint8_t)(randInt(128)),
        op,
        0x1A
    };
}

static std::vector<uint8_t> generateLocalTee() {
    return { 0x41, (uint8_t)(randInt(255)), 0x22, 0x00, 0x1A };
}

static std::vector<uint8_t> generateIfTrue() {
    return {
        0x41, 0x01,
        0x04, 0x40,
        0x41, (uint8_t)(randInt(64)),
        0x1A,
        0x0B
    };
}

static const std::vector<std::vector<uint8_t>> BASE_SAFE_GENOMES = {
    { 0x20, 0x00, 0x1A },
    { 0x20, 0x01, 0x1A },
    { 0x41, 0x00, 0x1A },
    { 0x41, 0x01, 0x1A },
};

static std::string describeSequence(const std::vector<uint8_t>& seq) {
    if (seq.empty()) return "empty";
    auto instrs = parseInstructions(seq.data(), seq.size());
    std::string desc;
    for (size_t i = 0; i < instrs.size(); i++) {
        if (i) desc += ", ";
        desc += getOpcodeName(instrs[i].opcode);
        if (instrs[i].argLen > 0) {
            desc += ' ';
            desc += std::to_string((int)instrs[i].args[0]);
        }
    }
    return desc;
}

static std::vector<uint8_t> flatten(const std::vector<Instruction>& instrs) {
    std::vector<uint8_t> out;
    out.reserve(instrs.size() * 2);
    for (const auto& inst : instrs) {
        out.push_back(inst.opcode);
        if (inst.argLen > 0)
            out.insert(out.end(), inst.args, inst.args + inst.argLen);
    }
    return out;
}

static std::vector<uint8_t> getGenome(
    const std::vector<std::vector<uint8_t>>& known,
    bool smart)
{
    float r = randF();
    float threshold = smart ? 0.95f : 0.7f;
    if (known.size() > 2 && r < threshold)
        return known[randInt((int)known.size())];

    float s = randF();
    if (s < 0.30f) return generateRandomConstDrop();
    if (s < 0.60f) return generateSafeMath();
    if (s < 0.80f) return generateLocalTee();
    if (s < 0.95f) return generateIfTrue();
    return BASE_SAFE_GENOMES[randInt((int)BASE_SAFE_GENOMES.size())];
}

EvolutionResult evolveBinary(
    const std::string&                       currentBase64,
    const std::vector<std::vector<uint8_t>>& knownInstructions,
    int                                      attemptSeed,
    MutationStrategy                         strategy)
{
    std::vector<uint8_t> bytes = base64_decode(currentBase64);

    // helper used repeatedly below: remove any CALL opcodes (0x10) along
    // with their immediates.  We do not generate new functions when we
    // mutate so any stray CALL would target a nonexistent index and trap.
    // Rather than throw we simply strip the instruction altogether.  The
    // bootloader no longer relies on CALL as a trigger either, so this is
    // safe.
    auto stripCalls = [&](std::vector<uint8_t>& seq) {
        std::vector<uint8_t> out;
        size_t i = 0;
        while (i < seq.size()) {
            uint8_t op = seq[i];
            if (op == 0x10) {
                auto leb = decodeLEB128(seq.data(), seq.size(), i + 1);
                i += 1 + leb.length;
                continue;
            }
            out.push_back(op);
            if (op == 0x41 || op == 0x20 || op == 0x21 || op == 0x22 ||
                op == 0x10) {
                auto leb = decodeLEB128(seq.data(), seq.size(), i + 1);
                for (int k = 0; k < leb.length; k++)
                    out.push_back(seq[i + 1 + k]);
                i += 1 + leb.length;
            } else if (op == 0x04) {
                // if + blocktype
                if (i + 1 < seq.size()) out.push_back(seq[i + 1]);
                i += 2;
            } else {
                i += 1;
            }
        }
        seq.swap(out);
    };

    // 1. Locate code section
    int ptr                    = 8;
    int codeSectionStart       = -1;
    int codeSectionContentStart = -1;

    while (ptr < (int)bytes.size()) {
        if (ptr + 1 >= (int)bytes.size()) break;
        uint8_t id       = bytes[ptr];
        auto    sizeData = decodeLEB128(bytes.data(), bytes.size(), ptr + 1);
        if (sizeData.length == 0) {
            throw std::runtime_error("Malformed LEB128 in section size");
        }
        if (id == 10) {
            codeSectionStart        = ptr;
            codeSectionContentStart = ptr + 1 + sizeData.length;
            break;
        }
        // advance, but ensure we don't overflow
        long nextPtr = (long)ptr + 1 + sizeData.length + (long)sizeData.value;
        if (nextPtr <= ptr || nextPtr > (long)bytes.size()) break;
        ptr = (int)nextPtr;
    }
    if (codeSectionStart == -1)
        throw std::runtime_error("Code section missing");

    // 2. Locate function body
    auto numFuncsData      = decodeLEB128(bytes.data(), bytes.size(), codeSectionContentStart);
    if (numFuncsData.length == 0)
        throw std::runtime_error("Malformed num-funcs LEB128");
    int  funcBodySizeOff   = codeSectionContentStart + numFuncsData.length;
    if (funcBodySizeOff > (int)bytes.size())
        throw std::runtime_error("Function body size offset out of bounds");
    auto funcBodySizeData  = decodeLEB128(bytes.data(), bytes.size(), funcBodySizeOff);
    if (funcBodySizeData.length == 0)
        throw std::runtime_error("Malformed func-body-size LEB128");
    int  funcContentStart  = funcBodySizeOff + funcBodySizeData.length;
    if (funcContentStart > (int)bytes.size())
        throw std::runtime_error("Function content start out of bounds");

    auto localCountData = decodeLEB128(bytes.data(), bytes.size(), funcContentStart);
    if (localCountData.length == 0 && funcContentStart < (int)bytes.size()) {
        // allow zero locals
    }
    int  instrPtr       = funcContentStart + localCountData.length;
    if (instrPtr > (int)bytes.size())
        throw std::runtime_error("Instruction pointer initialized out of bounds");

    for (int i = 0; i < (int)localCountData.value; i++) {
        auto countData = decodeLEB128(bytes.data(), bytes.size(), instrPtr);
        instrPtr += countData.length + 1;
    }

    int instructionStart = instrPtr;
    int funcEnd          = funcContentStart + (int)funcBodySizeData.value;
    int endOpIndex       = funcEnd - 1;

    // 3. Parse instructions
    std::vector<uint8_t> instrBytes(bytes.begin() + instructionStart,
                                     bytes.begin() + endOpIndex);
    auto parsedInstructions = parseInstructions(instrBytes.data(), instrBytes.size());

    // We deliberately *do not* strip existing CALL instructions from the
    // current kernel.  The base module may import host functions (for
    // example, `env.log`) whose arguments are pushed immediately before the
    // call; replacing the call with a NOP would leave those values on the
    // stack and result in validation failures.  We only need to worry about
    // calls arising from *mutations* (which may point to nonexistent targets),
    // so sanitisation is applied to the random genomes below when they are
    // generated.

    // 4. Evolution logic
    int action = attemptSeed % 4;
    std::vector<uint8_t> mutationSequence;
    std::vector<uint8_t> newInstructionsBytes;
    std::string          description;

    switch (action) {
        case (int)EvolutionAction::MODIFY:
        case (int)EvolutionAction::INSERT: {
            auto seq = getGenome(knownInstructions, strategy == MutationStrategy::SMART);
            stripCalls(seq); // ensure our mutation does not introduce new calls
            mutationSequence = seq;
            int  idx    = (int)(randF() * (float)(parsedInstructions.size() + 1));
            auto before = flatten(std::vector<Instruction>(
                parsedInstructions.begin(), parsedInstructions.begin() + idx));
            auto after  = flatten(std::vector<Instruction>(
                parsedInstructions.begin() + idx, parsedInstructions.end()));
            newInstructionsBytes = before;
            newInstructionsBytes.insert(newInstructionsBytes.end(), seq.begin(), seq.end());
            newInstructionsBytes.insert(newInstructionsBytes.end(), after.begin(), after.end());
            description = std::string(action == 0 ? "Modified" : "Inserted") +
                          ": [" + describeSequence(seq) + "] at " + std::to_string(idx);
            break;
        }
        case (int)EvolutionAction::DELETE: {
            if (!parsedInstructions.empty()) {
                int targetIdx  = -1;
                int deleteCount = 0;

                // Priority 1: delete NOPs
                for (int i = 0; i < (int)parsedInstructions.size(); i++) {
                    if (parsedInstructions[i].opcode == 0x01) {
                        targetIdx   = i;
                        deleteCount = 1;
                        description = "Deleted NOP at index " + std::to_string(i);
                        break;
                    }
                }

                // Priority 2: delete safe-math [Const,Const,Op,Drop]
                if (targetIdx == -1) {
                    for (int i = 0; i < (int)parsedInstructions.size() - 3; i++) {
                        static const uint8_t mathOps[] = {0x6A,0x6B,0x6C,0x71,0x72,0x73};
                        auto& p0 = parsedInstructions[i];
                        auto& p1 = parsedInstructions[i+1];
                        auto& p2 = parsedInstructions[i+2];
                        auto& p3 = parsedInstructions[i+3];
                        bool isMath = (p0.opcode == 0x41 && p1.opcode == 0x41 &&
                            std::find(std::begin(mathOps), std::end(mathOps), p2.opcode) != std::end(mathOps) &&
                            p3.opcode == 0x1A);
                        if (isMath && randF() < 0.6f) {
                            targetIdx   = i;
                            deleteCount = 4;
                            description = "Pruned math sequence [" + getOpcodeName(p2.opcode) + "]";
                            break;
                        }
                    }
                }

                // Priority 3: delete if-true blocks
                if (targetIdx == -1) {
                    for (int i = 0; i < (int)parsedInstructions.size() - 4; i++) {
                        auto& p0 = parsedInstructions[i];
                        auto& p1 = parsedInstructions[i+1];
                        auto& p4 = parsedInstructions[i+4];
                        if (p0.opcode == 0x41 && p0.argLen > 0 && p0.args[0] == 1 &&
                            p1.opcode == 0x04 && p4.opcode == 0x0B && randF() < 0.5f) {
                            targetIdx   = i;
                            deleteCount = 5;
                            description = "Pruned control flow block";
                            break;
                        }
                    }
                }

                // Priority 4: delete producer-consumer pairs
                if (targetIdx == -1) {
                    for (int i = 0; i < (int)parsedInstructions.size() - 1; i++) {
                        auto& p0 = parsedInstructions[i];
                        auto& p1 = parsedInstructions[i+1];
                        if ((p0.opcode == 0x41 || p0.opcode == 0x20 || p0.opcode == 0x22)
                            && p1.opcode == 0x1A) {
                            targetIdx   = i;
                            deleteCount = 2;
                            description = "Deleted balanced pair [" +
                                          getOpcodeName(p0.opcode) + ", drop]";
                            break;
                        }
                    }
                }

                if (targetIdx != -1) {
                    parsedInstructions.erase(
                        parsedInstructions.begin() + targetIdx,
                        parsedInstructions.begin() + targetIdx + deleteCount);
                } else {
                    description = "No safe deletion targets found (Skipped)";
                }
                mutationSequence.clear();
            } else {
                description = "Instruction set empty";
            }
            newInstructionsBytes = flatten(parsedInstructions);
            break;
        }
        case (int)EvolutionAction::ADD: {
            auto seq = getGenome(knownInstructions, strategy == MutationStrategy::SMART);
            stripCalls(seq);
            mutationSequence = seq;
            newInstructionsBytes = flatten(parsedInstructions);
            newInstructionsBytes.insert(newInstructionsBytes.end(), seq.begin(), seq.end());
            description = "Appended [" + describeSequence(seq) + "]";
            break;
        }
    }

    if (newInstructionsBytes.empty() && !parsedInstructions.empty())
        newInstructionsBytes = flatten(parsedInstructions);

    // quick sanity: the mutation sequence itself should not contain an
    // explicit `unreachable` opcode.  parseInstructions will treat any
    // 0x00 byte as such, so we can detect bad genomes early and abort.
    {
        auto check = parseInstructions(newInstructionsBytes.data(),
                                       newInstructionsBytes.size());
        for (const auto& inst : check) {
            if (inst.opcode == 0x00) {
                throw std::runtime_error("Evolution generated unreachable opcode");
            }
        }
    }

    // 5. Reconstruct binary
    std::vector<uint8_t> preInstructions(bytes.begin() + funcContentStart,
                                          bytes.begin() + instructionStart);
    std::vector<uint8_t> postInstructions(bytes.begin() + endOpIndex, bytes.end());

    uint32_t newFuncBodyLen = (uint32_t)(preInstructions.size() +
                                          newInstructionsBytes.size() +
                                          postInstructions.size());

    if (newFuncBodyLen > 32768)
        throw std::runtime_error("Evolution Limit: 32KB");

    auto newFuncBodySizeEnc = encodeLEB128(newFuncBodyLen);

    std::vector<uint8_t> preFuncSize(bytes.begin() + codeSectionContentStart,
                                      bytes.begin() + funcBodySizeOff);

    uint32_t newSectionContentLen = (uint32_t)(preFuncSize.size() +
                                                newFuncBodySizeEnc.length +
                                                newFuncBodyLen);
    auto newSectionSizeEnc = encodeLEB128(newSectionContentLen);

    std::vector<uint8_t> preCode(bytes.begin(), bytes.begin() + codeSectionStart + 1);

    std::vector<uint8_t> newBytes;
    newBytes.reserve(preCode.size() + newSectionSizeEnc.length + preFuncSize.size() +
                     newFuncBodySizeEnc.length + preInstructions.size() +
                     newInstructionsBytes.size() + postInstructions.size());

    auto appendVec = [&](const std::vector<uint8_t>& v) {
        newBytes.insert(newBytes.end(), v.begin(), v.end());
    };
    auto appendLEB = [&](const LEB128Encoded& e) {
        newBytes.insert(newBytes.end(), e.data, e.data + e.length);
    };

    appendVec(preCode);
    appendLEB(newSectionSizeEnc);
    appendVec(preFuncSize);
    appendLEB(newFuncBodySizeEnc);
    appendVec(preInstructions);
    appendVec(newInstructionsBytes);
    appendVec(postInstructions);

    // Before handing the new binary back to the caller we perform a
    // *validation* pass.  Several hard-to-debug issues (including the
    // "stuck at gen 49" case) were caused by malformed modules getting
    // past the evolution layer and then immediately trapping when run.
    // The cost of instantiating/running the candidate is small compared
    // to the overall generation time, and catching errors here allows the
    // caller's try/catch wrappers to reject the mutation cleanly and
    // continue searching for a different sequence.
    std::string b64 = base64_encode(newBytes);
    {
        try {
            WasmKernel wk;
            // no callbacks needed for validation
            wk.bootDynamic(b64, {}, {}, {}, {}, {});
            // execute an empty run to ensure the entry point is reachable
            wk.runDynamic("");
            wk.terminate();
        } catch (const std::exception& e) {
            throw EvolutionException(std::string("Validation failed: ") + e.what(), b64);
        }
    }

    // OPTIONAL: run the candidate kernel once with a weight callback so we
    // can observe any internal predictor outputs.  This demonstrates the
    // in-kernel sequence model executing at mutation time; callers can use
    // the resulting floats to bias selection if desired.
    std::vector<float> feedback;
    try {
        WasmKernel wk;
        auto weightCb = [&](uint32_t a, uint32_t b) {
            float f1, f2;
            std::memcpy(&f1, &a, sizeof(f1));
            std::memcpy(&f2, &b, sizeof(f2));
            feedback.push_back(f1);
            feedback.push_back(f2);
        };
        wk.bootDynamic(b64, {}, {}, {}, weightCb, {});
        wk.runDynamic(b64);
        wk.terminate();
    } catch (...) {
        // ignore errors; feedback will remain empty
    }

    EvolutionResult result{
        std::move(b64),
        mutationSequence,
        (EvolutionAction)action,
        description,
        std::move(feedback)
    };
    return result;
}
