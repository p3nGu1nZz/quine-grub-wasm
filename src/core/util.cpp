#include "util.h"
#include "base64.h"

#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <ctime>
#include <filesystem>
#include <unordered_map>

std::string stateStr(SystemState s) {
    switch (s) {
        case SystemState::IDLE:            return "IDLE";
        case SystemState::BOOTING:         return "BOOTING";
        case SystemState::LOADING_KERNEL:  return "LOADING_KERNEL";
        case SystemState::EXECUTING:       return "EXECUTING";
        case SystemState::VERIFYING_QUINE: return "VERIFYING_QUINE";
        case SystemState::SYSTEM_HALT:     return "SYSTEM_HALT";
        case SystemState::REPAIRING:       return "REPAIRING";
    }
    return "UNKNOWN";
}

// -----------------------------------------------------------------------------
// DPI scaling utilities
// -----------------------------------------------------------------------------

// compute a font/UI scale based purely on window size
float computeDpiScale(SDL_Window* window) {
    if (!window) return 1.0f;
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    if (w <= 0 || h <= 0) return 1.0f;
    // baseline resolution (chosen empirically)
    const float baseW = 1400.0f;
    const float baseH = 900.0f;
    float sx = (float)w / baseW;
    float sy = (float)h / baseH;
    float scale = std::max(sx, sy);
    if (scale < 1.0f) return 1.0f;
    return std::min(scale, 2.0f);
}

const std::vector<uint8_t>& decodeBase64Cached(const std::string& b64) {
    static std::unordered_map<std::string, std::vector<uint8_t>> cache;
    auto it = cache.find(b64);
    if (it != cache.end()) return it->second;
    auto decoded = base64_decode(b64);
    auto res = cache.emplace(b64, std::move(decoded));
    return res.first->second;
}

std::string randomId() {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 35);
    const char* ch = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::string id(9, ' ');
    for (char& c : id) c = ch[dist(rng)];
    return id;
}

std::string nowIso() {
    using namespace std::chrono;
    auto tp = system_clock::now();
    auto tt = system_clock::to_time_t(tp);
    auto ms = duration_cast<milliseconds>(tp.time_since_epoch()) % 1000;
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &tt);
#else
    gmtime_r(&tt, &utc);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", &utc);
    std::ostringstream ss;
    ss << buf << '.' << std::setw(3) << std::setfill('0') << ms.count() << 'Z';
    return ss.str();
}

std::string nowFileStamp() {
    using namespace std::chrono;
    auto tp = system_clock::now();
    auto tt = system_clock::to_time_t(tp);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &tt);
#else
    gmtime_r(&tt, &utc);
#endif
    char buf[20];
    std::strftime(buf, sizeof buf, "%Y%m%d_%H%M%S", &utc);
    return buf;
}

std::string executableDir() {
    try {
        auto exe = std::filesystem::read_symlink("/proc/self/exe");
        if (exe.has_parent_path())
            return exe.parent_path().string();
    } catch (...) {}
    return std::filesystem::current_path().string();
}

std::filesystem::path sequenceDir(const std::string& runId) {
    // keep only alphanumeric characters to avoid traversal/escaping
    std::string cleaned;
    for (char c : runId) {
        // allow alphanumeric and underscore (runId uses underscores).
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
            cleaned.push_back(c);
    }
    if (cleaned.empty()) cleaned = "run";
    std::filesystem::path exe = executableDir();
    std::filesystem::path root = exe;
    // match telemetryRoot logic: if the executable resides in a "test" or
    // "bin" subdirectory, assume the real base is one level up so the
    // final path ends up under build/<target>/bin rather than
    // build/<target>/bin/bin.
    auto fname = exe.filename().string();
    if (fname == "test" || fname == "bin")
        root = exe.parent_path();
    std::filesystem::path p = root / "bin" / "seq" / cleaned;
    return p;
}

std::string sanitizeRelativePath(const std::string& input) {
    if (input.empty()) return "";
    // reject any literal parent-traversal tokens in the raw input
    if (input.find("..") != std::string::npos) return "";
    namespace fs = std::filesystem;
    fs::path p = fs::path(input).lexically_normal();
    // absolute paths are not allowed
    if (p.is_absolute()) return "";
    // reject any parent path references after normalization as a double-check
    for (auto& part : p) {
        if (part == "..")
            return "";
    }
    return p.string();
}
