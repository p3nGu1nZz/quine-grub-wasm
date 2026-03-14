#include "exporter.h"
#include "base64.h"
#include "util.h"
#include "wasm/parser.h"

#include <cstdio>
#include <iostream>
#include <sstream>
#include <iomanip>

std::string buildReport(const ExportData& d) {
    // ── Disassembly ────────────────────────────────────────────────────────────
    std::string disasm = "No instructions available.";
    if (!d.instructions.empty()) {
        std::ostringstream ss;
        for (int i = 0; i < (int)d.instructions.size(); i++) {
            const auto& inst = d.instructions[i];
            std::string name = getOpcodeName(inst.opcode);
            std::string args;
            for (int ai = 0; ai < inst.argLen; ++ai) {
                if (!args.empty()) args += ' ';
                char hex[8];
                std::snprintf(hex, sizeof(hex), "0x%02X", (unsigned)inst.args[ai]);
                args += hex;
            }
            ss << std::setw(3) << std::setfill('0') << i
               << " | 0x" << std::uppercase << std::hex
               << std::setw(4) << std::setfill('0') << inst.originalOffset
               << " | " << std::setw(12) << std::setfill(' ') << std::left
               << name << " " << args << '\n';
        }
        disasm = ss.str();
    }

    // ── Hex dump ──────────────────────────────────────────────────────────────
    auto raw = base64_decode(d.currentKernel);
    if (raw.empty() && !d.currentKernel.empty()) {
        std::cerr << "Exporter: decoded kernel is empty (input base64 length="
                  << d.currentKernel.size() << ")\n";
    }
    std::ostringstream hd;
    for (size_t i = 0; i < raw.size(); i += 16) {
        hd << "0x" << std::uppercase << std::hex
           << std::setw(4) << std::setfill('0') << i << "  ";
        std::string ascii;
        for (size_t j = 0; j < 16; j++) {
            if (i + j < raw.size()) {
                hd << std::uppercase << std::hex
                   << std::setw(2) << std::setfill('0') << (int)raw[i + j] << ' ';
                char c = (char)raw[i + j];
                ascii += (c >= 32 && c <= 126) ? c : '.';
            } else {
                hd << "   ";
                ascii += ' ';
            }
        }
        hd << " |" << ascii << "|\n";
    }

    // ── History log ───────────────────────────────────────────────────────────
    std::ostringstream hist;
    for (const auto& h : d.history) {
        std::string ts = h.timestamp.size() > 11 ? h.timestamp.substr(11, 12) : h.timestamp;
        hist << "[GEN " << std::setw(4) << std::setfill('0') << h.generation << "] "
             << ts << " | " << std::setw(10) << std::setfill(' ') << std::left << h.action
             << " | " << (h.success ? "OK  " : "FAIL") << " | " << h.details << '\n';
    }

    // ── Era name ──────────────────────────────────────────────────────────────

    // ── Assemble report ───────────────────────────────────────────────────────
    std::ostringstream out;
    out << "WASM QUINE BOOTLOADER - SYSTEM HISTORY EXPORT\n"
        << "Generated: " << nowIso() << '\n'
        << "Final Generation: " << d.generation << '\n'
        << "Kernel Size: " << raw.size() << " bytes\n";
    if (d.mutationsAttempted || d.mutationsApplied) {
        out << "Mutations Attempted: " << d.mutationsAttempted << '\n'
            << "Mutations Applied: " << d.mutationsApplied << '\n'
            << "Mutation Breakdown: insert=" << d.mutationInsert
            << ", delete=" << d.mutationDelete
            << ", modify=" << d.mutationModify
            << ", append=" << d.mutationAdd << "\n";
    }
    if (!d.trapCode.empty()) {
        out << "Traps: " << d.trapCode << "\n";
    }
    if (d.genDurationMs > 0.0) {
        out << "Gen Duration: " << d.genDurationMs << " ms\n";
    }
    if (d.kernelSizeMin || d.kernelSizeMax) {
        out << "Kernel Size Min/Max: " << d.kernelSizeMin
            << "/" << d.kernelSizeMax << "\n";
    }
    if (d.heuristicBlacklistCount) {
        out << "Heuristic Blacklist Entries: " << d.heuristicBlacklistCount << "\n";
    }
    if (d.advisorEntryCount) {
        out << "Advisor Entries: " << d.advisorEntryCount << "\n";
    }
    if (!d.instances.empty()) {
        out << "INSTANCES: " << d.instances.size() << "\n";
        for (const auto& inst : d.instances) {
            out << "  " << inst << "\n";
        }
    }
    // Note: exporter itself doesn't perform file I/O; caller should handle
    // errors when writing the string.  This function now warns if decoded
    // raw bytes are unexpectedly empty.
    out << "\n"
        << "CURRENT KERNEL (BASE64):\n"
        << std::string(80, '-') << '\n'
        << d.currentKernel << '\n'
        << std::string(80, '-') << "\n\n"
        // include decoded opcode sequence for models that want to consume it
        << "OPCODE SEQUENCE:\n"
        << std::string(80, '-') << '\n';
    {
        // replicate minimal sequence extraction: decode, parse code section
        auto bytes = base64_decode(d.currentKernel);
        auto instrs = extractCodeSection(bytes);
        for (auto& inst : instrs) {
            out << (int)inst.opcode << " ";
        }
        out << '\n';
    }
    out << std::string(80, '-') << "\n\n"
        << "HEX DUMP:\n"
        << std::string(80, '-') << '\n'
        << hd.str()
        << std::string(80, '-') << "\n\n"
        << "DISASSEMBLY:\n"
        << std::string(80, '-') << '\n'
        << "IDX | ADDR   | OPCODE       ARGS\n"
        << std::string(80, '-') << '\n'
        << disasm
        << std::string(80, '-') << "\n\n"
        << "HISTORY LOG:\n"
        << std::string(80, '-') << '\n'
        << hist.str()
        << std::string(80, '-') << '\n'
        << "END OF REPORT\n";

    return out.str();
}
