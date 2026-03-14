# Command-line Interface – Specification

## Purpose

Define the behaviour of the bootloader executable's command-line arguments. This spec ensures the launcher and the scripts remain in sync, and that automated tests can validate parsing logic.

## Inputs

- `--gui` (default when no flag present) – launch SDL3/ImGui GUI.
- `--headless`, `--no-gui`, `--nogui` – disable GUI and run in headless/terminal mode.
- `--fullscreen` – request a maximized (borderless) window when GUI is enabled; this is *not* exclusive fullscreen.
- `--windowed` – request a windowed window when GUI is enabled.

Additional options implemented by `parseCli()`:

- `--telemetry-level=<none|basic|full>` – control verbosity of telemetry exports; `none` disables files, `basic` writes header+size, `full` includes all sections (mutations, traps, etc.).  The default level is now **full** to aid debugging and analysis of evolving kernels.
- `--telemetry-dir=<path>` – override the default location used for exports.  When unspecified the base path is derived from the **executable’s directory**, which may itself be a `bin` subdirectory (e.g. `build/linux-debug/bin`).  The telemetry root is then `<exe_dir>/bin/seq/<runid>` with an extra `bin` stripped if necessary to avoid producing `bin/bin`.  This avoids accidentally creating a `bin/` folder in the current working directory.
- `--telemetry-format=<text|json>` – choose the export file format.  `text` (the default) produces the traditional plain‑text report; `json` emits a minimal JSON object for easier programmatic parsing.
- `--mutation-strategy=<random|blacklist|smart>` – choose the evolution sampling policy.  `blacklist` interacts with the mutation heuristic but does not itself enable it.
- `--heuristic=<none|blacklist|decay>` – enable the trap-avoidance blacklist, with `decay` allowing entries to expire after successful generations.
- `--profile` – log per‑generation timing and memory usage.
- `--max-gen=<n>` – exit after `n` successful generations (0=unlimited); handy for CI tests.
- `--max-run-ms=<n>` – abort as soon as the process has been running for roughly `n` milliseconds.  Useful as a simple watchdog when invoking the bootloader from external harnesses or when integrating into services that impose time limits.
- `--max-exec-ms=<n>` – limit the duration of each WASM kernel execution to about `n` milliseconds.  When a kernel exceeds the threshold it is forcibly terminated and the generation is treated as a failure; this protects against infinite loops or runaway code.  This feature is implemented via a fork‑based watchdog on Unix platforms.
- `--save-model=<path>` / `--load-model=<path>` – persist or restore the trainer model to/from disk.
- `--kernel=<glob|seq>` – choose which built-in WASM module to use as the seed for evolution.  `glob` is the original quine; `seq` is a minimal recurrent kernel that reports internal weights via `env.record_weight` and is executed once for each candidate during mutation.  EvolutionResults record any floats emitted by such kernels in their `weightFeedback` field.
- `--kernel=<glob|seq>` – choose the initial kernel used for evolution.  `glob`
  picks the standard quine, while `seq` selects a minimal RNN that reports
  its hidden weights via `env.record_weight`.  This kernel will also be
  executed once per candidate during `evolveBinary()` and the resulting
  floats are made available in the `EvolutionResult` for experimentation.

Unrecognised flags or malformed values generate a warning on stderr and set the `parseError` field in the returned `CliOptions`.  Completely unknown options are otherwise ignored so that wrapper scripts can forward extra arguments to the bootloader.

### Warning behaviour

If a flag that expects a finite set of values is given an unrecognised token (e.g. `--telemetry-level=foo` or `--heuristic=bar`), `parseCli()` emits a `Warning:` line to stderr and sets the `parseError` flag in the returned `CliOptions`.  The bootloader then continues with the default for that option; wrapper scripts or tests can check `parseError` and abort if desired.  Completely unknown options likewise produce a warning but are otherwise ignored so that `scripts/run.sh` can transparently forward extra arguments to the executable.

> **Note:** earlier versions of this spec accidentally listed the
> `--heuristic` values in the order `blacklist|none|decay` which did not
> match the parser.  The documentation above has been corrected and a unit
> test (`[cli]` cases) now verifies consistent parsing.

Flags that accept values may be written either as `--flag=value` or the traditional `--flag value`; the parser's `extractValue()` helper handles both forms and advances `argv` appropriately.

## Behaviour

1. Parse `argc`/`argv` in `parseCli()`.
2. Default state: `useGui=true`, `fullscreen=true`.
3. Flags may override state; the last flag wins for contradictory options (e.g. `--windowed --fullscreen`).
4. The main loop in `main.cpp` interprets `CliOptions` to decide whether to initialize SDL video or just the timer subsystem, and whether to add `SDL_WINDOW_MAXIMIZED` to window flags (formerly SDL_WINDOW_FULLSCREEN_DESKTOP in SDL2).
5. Headless mode still constructs an `App` object and runs `app.update()` on a fixed 60 Hz timer so the core logic is exercised; no renderer or GUI is created.

## Constraints

- SDL and ImGui dependencies must only be initialised if `useGui` is true, to allow running on CI with no display.
- The `--fullscreen` flag currently maps to `SDL_WINDOW_FULLSCREEN_DESKTOP` which does not change the display mode; it may be changed in future if windowed scaling issues arise.

## Open Questions

- (answered) the `--max-run-ms` flag now exists and is handled by the launcher; external tools such as `timeout` are still valid alternatives but the built-in mechanism provides a lightweight option for CI and headless runs.

