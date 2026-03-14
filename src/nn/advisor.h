#pragma once

#include <string>
#include <vector>
#include <cstdint>

// simple record extracted from a telemetry export
struct TelemetryEntry {
    int generation = 0;
    std::string kernelBase64;
    std::string trapCode;
    // decoded opcode sequence (filled by Advisor) for convenience
    std::vector<uint8_t> opcodeSequence;
};

// Advisor loads all telemetry exports under a given base
// directory and makes them available for training/advice.
class Advisor {
public:
    explicit Advisor(const std::string& baseDir = "bin/seq");

    // number of entries successfully parsed
    size_t size() const { return m_entries.size(); }

    const std::vector<TelemetryEntry>& entries() const { return m_entries; }

    // return a safety score in [0,1] for a candidate mutation sequence
    float score(const std::vector<uint8_t>& seq) const;

    // number of telemetry entries loaded from disk
    size_t entryCount() const { return m_entries.size(); }

    // test helper: insert an entry directly without reading from filesystem
    void test_addEntry(const TelemetryEntry& e) { m_entries.push_back(e); }

    // write the current advisor entries to disk (simple text format).
    // Returns true on success.
    bool dump(const std::string& path) const;

private:
    void scanDirectory(const std::string& runDir);
    void parseFile(const std::string& path);

    std::vector<TelemetryEntry> m_entries;
};
