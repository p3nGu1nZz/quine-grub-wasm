# Porting Report: C++ port review

## Overview
I reviewed the C++ `src/` files and compared with the original `web/` reference. The port largely matches the design, but there are a few critical issues that need immediate attention before running the app in CI or production.

## Critical findings

1. `src/main.cpp` — incorrect SDL initialization check causes immediate exit on success. The code currently uses `if (!SDL_Init(...)) { /* error */ }` which treats success as failure.

2. `src/logger.cpp` — the global signal handler `flushSignalHandler` calls `AppLogger::flush()` which uses file IO. Calling non-async-signal-safe functions from a signal handler is undefined behavior and can crash the process.

3. Runtime/documentation mismatch — README describes headless / terminal mode and `--gui` flag; `src/main.cpp` always initializes GUI and does not parse CLI arguments for headless mode.

## Suggested changes (no code changes in this PR)

- Fix SDL_Init check in `src/main.cpp`.
- Replace signal handler flush with an atomic flag, and perform flush in `App::update()` or main loop.
- Add CLI argument parsing and optional headless terminal renderer or update docs to reflect current behavior.

## Acceptance criteria
- [ ] `SDL_Init` check corrected and verified by running the app.
- [ ] Signal handler made safe: no non-async-signal-safe calls from signal handlers; flush occurs on main thread.
- [ ] CLI parsing added with `--gui` option or docs updated to match implementation.
- [ ] New issue template added under `.github/ISSUE_TEMPLATE/porting_report.md`.

## How to file the GitHub issue
Use the contents of this file as the issue body. If you want to create the issue programmatically use the `docs/PULL_REQUEST_DESCRIPTION.md` and `docs/README_PORTING_SUMMARY.md` as reference materials.

