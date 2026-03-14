#pragma once

#include "types.h"
#include <vector>
#include <string>
#include <SDL3/SDL.h>
#include <filesystem>

// Convert enum values to display strings
std::string stateStr(SystemState s);

// DPI scaling helpers used by the GUI layer.  The only public
// helper is computeDpiScale(), which derives a scale factor solely from the
// resolution of an SDL window; the previously‑exported `dpiScaleFromDpi`
// helper was removed because it is no longer called by production code.
// compute scale using an SDL_Window handle
float computeDpiScale(SDL_Window* window);

// Generate a short random alphanumeric ID (9 chars)
std::string randomId();

// Decode a base64 string with an internal cache to avoid repeated work.
const std::vector<uint8_t>& decodeBase64Cached(const std::string& b64);

// Return current UTC time as ISO-8601 string (e.g. "2026-01-02T03:04:05.678Z")
std::string nowIso();

// Return a filename-safe UTC timestamp (e.g. "20260102_030405")
std::string nowFileStamp();

// Return directory containing the running executable (Linux-specific).
// Falls back to current_path() if unreadable.
std::string executableDir();

// Given a run identifier, return the sequence export directory where
// the telemetry for that run should be written.  The result is
// "<exe_dir>/bin/seq/<runId>".
std::filesystem::path sequenceDir(const std::string& runId);

// Ensure a user-supplied directory path is safe to use. The returned
// string will be empty if the input is absolute, contains ".." segments,
// or would escape the working directory.  This is used for telemetry
// directory overrides passed via CLI.  Caller may log a warning on
// failure.
std::string sanitizeRelativePath(const std::string& input);
