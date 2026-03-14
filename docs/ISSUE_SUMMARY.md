# Issue: Porting Review — Fixes needed before running

This file is an in-repo copy of the issue body to be opened in GitHub Issues. It documents the problems found during the C++ port review and suggests fixes.

## Summary
- `src/main.cpp`: wrong SDL_Init check.
- `src/logger.cpp`: signal handler calls IO.
- Missing CLI/headless mode.

## Suggested fixes
- Fix SDL_Init check
- Make signal handler safe
- Add CLI parsing or update docs

## Acceptance criteria
- SDL_Init corrected and verified
- Signal handler uses atomic flag and main-thread flush
- CLI parsing or docs updated
