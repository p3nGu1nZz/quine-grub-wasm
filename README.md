# quine-grub-wasm

Formerly known as **WASM-Quine-Bootload**, this repository now hosts the
`quine-grub-wasm` bootloader: a C++17/SDL3 desktop simulation that evolves
self-replicating WebAssembly kernels inside an ImGui HUD while also exposing
headless and telemetry-first workflows. It bundles the mutation engine,
telemetry exporter, heuristic blacklists, trainer, and runtime helpers into a
single executable, with scripts to bootstrap the dependencies, build, run, and
test the experience.

## Overview

Every frame the FSM (`App::BootFsm`) steps through the boot/execute/verify
sequence, reloads the current kernel via `WasmKernel`, validates the quine
output over `env.log`, mutates the verified candidate, and either reboots or
repairs depending on the outcome. Successful generations are exported to
`bin/seq/<runid>/gen_<n>.txt`, which drive the neural trainer, the heuristic
blacklist, and the advisor panel. After 50 successful generations the app
automatically pauses evolution, reloads the latest telemetry, and runs a
training cycle over the autoregressive policy (inputs are opcode counts and
sequence windows, the LSTM stays fixed, and dense layers are updated via SGD).

The GUI renders a multi-panel HUD: top-bar state/status, log panel, kernel
source diff, instruction stream, memory heatmap, neural weight grids, and an
instances overview (spawned runtimes are registered via `env.spawn` and can be
killed through `env.kill_instance`). Runtime logs land in `bin/logs/`, telemetry
live in `bin/seq`, and the launcher scripts keep everything consistent across
working directories.

## Highlights

| Area | What it gives you |
|---|---|
| *Boot loop* | SDL3 event loop → `App::update()` → `BootFsm` → `startBoot/verify` → `evolveBinary` with INSERT/DELETE/MODIFY/APPEND. |
| *Mutation + heuristics* | `--mutation-strategy` chooses RANDOM, BLACKLIST, or SMART sampling; `--heuristic` toggles the blacklist that forbids known-bad edits and can decay over time. |
| *CLI/config* | Flags for telemetry verbosity/format, watchdogs (`--max-gen`, `--max-run-ms`, `--max-exec-ms`), profiling, kernel selection (`glob`/`seq`), and model checkpointing (`--save-model`, `--load-model`). Unknown flag values emit warnings and set `parseError`. |
| *GUI & monitoring* | Dear ImGui via SDL3 presents logs, instruction stream, kernel diff, heatmaps, neural weight grids, and spawned-instance controls; DPI scale is clamped to [1.0, 2.0] with an extra UI boost (≈1.5×). |
| *Telemetry* | Exports include headers, mutation/trap stats, opcode sequences, blacklist counts, instance sections, hex dump, disassembly, and history log plus a JSON variant when `--telemetry-format=json` is passed. |
| *Training & advisor* | Automatic training cycles run when 50 generations complete; learned weights are saved as `model_checkpoint.dat`, replay buffer resets, and GUI shows a “Saving model...” countdown. |

## Build & dependencies

1. **Bootstrap third-party libs** – `bash scripts/setup.sh` installs system packages,
   clones ImGui/wasm3, builds SDL3 (Linux + optional Windows cross-compile),
   and compiles Catch2 for tests. Pass `windows` to also install MinGW-w64/SDL3
   for `scripts/build.sh windows-*` targets. Run `scripts/setup.sh --clean` to
   drop `external/`, `bin/`, and then rebuild from scratch.
2. **Override compilers** – export `CC`/`CXX` before invoking `build.sh` to force
   specific toolchains; the script propagates them through CMake.
3. **Build targets** – `scripts/build.sh [linux-debug|linux-release|windows-debug|windows-release]`.
   `--clean` wipes `build/`, `CMakeFiles`, cached telemetry/logs, and `bin/`.

| Target | Platform | Output |
|---|---|---|
| `linux-debug` | Linux | `build/linux-debug/bin/bootloader` |
| `linux-release` | Linux | `build/linux-release/bin/bootloader` |
| `windows-debug` | Windows via MinGW-w64 | `build/windows-debug/bootloader.exe` |
| `windows-release` | Windows via MinGW-w64 | `build/windows-release/bootloader.exe` |

The build script automatically detects SDL3 under `external/SDL3/linux` (or
system SDL3 via pkg-config) and `external/SDL3/windows`. Running from WSL/WSL2
with `bash scripts/setup.sh windows` prepares the MinGW toolchain and headers.

## Running

Use `bash scripts/run.sh <args>` to build (if needed) and launch the bootloader.
When no arguments are provided the launcher defaults to `--gui`; pass `--headless`
(or `--no-gui`, `--nogui`) to exercise the FSM without creating a window. `--monitor`
runs the bootloader in the background and tails `bin/logs/*.log`, showing the exit
code plus the last 20 lines of each log file.

The launcher prints where logs (`bin/logs/`) and telemetry (`bin/seq/<runid>/`)
will be generated, ensuring the exported data always lives under the build
directory. The binary’s working directory switches to the build target but the
export paths are derived from the executable location to avoid `bin/bin` traps.

## Command-line options

Primary flags are documented in [`docs/specs/spec_cli.md`](docs/specs/spec_cli.md):

| Flag | Description |
|---|---|
| `--telemetry-level=<none|basic|full>` | Controls how much of the export file is written; default is `full`. |
| `--telemetry-format=<text|json>` | Choose between human-readable text and minimal JSON exports. |
| `--telemetry-dir=<path>` | Override the root directory for generated reports (default derived from `<exe_dir>/bin/seq/<runid>`). |
| `--mutation-strategy=<random|blacklist|smart>` | Bias the mutation sampler; `blacklist` reuses the heuristic list. |
| `--heuristic=<none|blacklist|decay>` | Enable/disable the blacklist; `decay` allows entries to expire after successful generations. |
| `--profile` | Record per-generation timing/memory stats. |
| `--max-gen=<n>` | Exit after `n` successful generations (0 = unlimited). |
| `--max-run-ms=<n>` | Watchdog that aborts after ~`n` milliseconds of runtime. |
| `--max-exec-ms=<n>` | Limit each WASM execution to roughly `n` milliseconds (Unix only). |
| `--save-model=<path>` / `--load-model=<path>` | Persist or restore the trainer checkpoint. |
| `--kernel=<glob|seq>` | Seed evolution with either the quine kernel or the recurrent sequence prototype. |

Unknown flags emit a warning, set a `parseError` marker in `CliOptions`, and otherwise
let wrapper scripts forward additional arguments safely.

## Telemetry & logging

Telemetry exports live under `build/<target>/bin/seq/<runid>/` and follow the format
specified in [`docs/specs/spec_telemetry.md`](docs/specs/spec_telemetry.md). Each
`gen_<n>.txt` contains headers, mutation/trap stats, opcode sequences, blacklist
counts, optional instance lists (spawned via `env.spawn`), the current kernel base64,
hex dump, disassembly, and a history log. JSON exports mirror the same sections via
key/value objects. Log files stream to `bin/logs/` and can be tailed while the app runs.

Mutation heuristics (`docs/specs/spec_heuristics.md`) maintain a small blacklist of
structural patterns that caused traps; entries are persisted in `blacklist.txt` inside
the telemetry directory and reloaded on startup. `env.spawn` and `env.kill_instance` are
available to kernels for sibling management (`docs/specs/spec_multi.md`).

## Testing

`bash scripts/test.sh [BUILD_TARGET]` builds the requested target (default `linux-debug`),
runs every `bin/test_*` binary, and falls back to `ctest` if none are present. Successful
runs print a summary panel with total executables, assertion counts, and elapsed seconds.
Tests are written with Catch2 (downloaded/built by `scripts/setup.sh`), so the helper also
verifies that `external/Catch2/install/include/catch2/catch_all.hpp` is present.

## Documentation

Detailed specs live in `docs/`:

- `design.md` and `architecture.md` describe the boot loop, FSM, kernel parser, exporter, GUI panels, and dependency graph.
- `docs/specs/` hosts deep dives on CLI semantics, telemetry format, heuristic blacklist, multi-instance APIs, neural trainer, and reboot ideas (`spec_cli.md`, `spec_telemetry.md`, `spec_heuristics.md`, `spec_multi.md`, `spec_neural.md`, `spec_quine_reboot.md`).
- `docs/workflows.md` outlines the Copilot master/dev workflows (`.github/prompts/master_workflow.prompt.md`, `dev_workflow.prompt.md`).
- `docs/PORTING_REPORT.md`, `docs/README_PORTING_SUMMARY.md`, and `docs/ISSUE_SUMMARY.md` document outstanding porting concerns (SDL init check, signal handler safety, headless mode mismatch) and suggested acceptance criteria.

Agent skills live under `.github/` and `.github/prompts`; refer to those folders for automation recipes.

## Layout

```
. 
├── CMakeLists.txt                # Single executable (C++17, Ninja, wasm3 + SDL3 libs)
├── src/                          # Core runtime, logger, FSM, exporter, heuristic, GUI, wasm parser/evolution/kernel
├── scripts/                      # `setup.sh`, `build.sh`, `run.sh`, `test.sh`
├── docs/                         # Architecture/design/spec/workflow/porting notes
├── external/                     # Populated by `scripts/setup.sh` (SDL3, ImGui, wasm3, Catch2)
└── cmake/                        # Toolchains (e.g. `toolchain-windows-x64.cmake`)
```

`external/` is ignored by Git. Build artifacts and logs land below `build/<target>/bin/`.

## Known issues & reminders

- Porting review (`docs/PORTING_REPORT.md` / `docs/ISSUE_SUMMARY.md`) still flags `SDL_Init` conditionals, the signal handler flush, and the need to keep the documented headless mode in sync with `main.cpp`.
- JSON telemetry exports historically omitted a comma between `heuristicBlacklistCount` and `advisorEntryCount`; parsers should tolerate this until fixed.
- Training weights remain small (dense layers only, LSTM gates untouched) and checkpointing is explicit via `--save-model`/`--load-model`.

Help or suggestions? Open an issue on this repository or contribute through the documented workflows.
