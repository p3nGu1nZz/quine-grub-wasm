# Issue: Porting Review — Fixes needed before running

This file is an in-repo copy of the original issue body. It documents the
problems found during the C++ port review and tracks their resolutions.

## Summary (historical)
- `src/main.cpp`: wrong SDL_Init check.
- `src/log.cpp`: signal handler called IO.
- Missing CLI/headless mode.

## Status
All issues below have been resolved:

- [x] SDL_Init check corrected and verified
- [x] Signal handler replaced with advisory flock + main-thread flush
- [x] Full CLI parsing added (`--headless`, `--windowed`, `--telemetry-*`, etc.)
