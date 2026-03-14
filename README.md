# quine-grub-wasm

A C++17 desktop simulation where self-replicating WebAssembly kernels evolve in real time. Watch the mutation engine generate, validate, and run genetically modified WASM binaries — with a neural network policy learning which mutations survive — all rendered live through an ImGui HUD.

This project was formerly known as **WASM-Quine-Bootload**.

---

## What it does

At its core, a quine is a program that prints its own source code. Here we take that idea further: the quine kernel is a minimal WASM binary that calls back to the host with its own base64-encoded representation. If the host receives exactly the right bytes, the quine succeeds and we mutate the kernel, creating a new descendant. That descendant runs next. If it also succeeds, the generation count ticks up. If it traps or returns wrong data, we repair back to the last known-good kernel and try a different mutation.

After 50 successful generations the simulation pauses evolution and trains a small neural network on the accumulated telemetry — opcode histograms, sequence windows, trap history — so future mutations are guided by what's worked before. Then evolution resumes.

## Architecture in 30 seconds

```
SDL3 event loop
  └── App::update()
        ├── BootFsm  (IDLE → BOOTING → LOADING_KERNEL → EXECUTING → VERIFYING_QUINE)
        ├── WasmKernel  (wasm3 runtime — boots, links host imports, calls run())
        ├── evolveBinary()  (INSERT / DELETE / MODIFY / APPEND on code section)
        ├── Trainer  (SGD on dense layers, LSTM for temporal context)
        └── Exporter  (gen_<n>.txt + kernel_<n>.b64 under bin/seq/<runid>/)
```

The GUI is a full-window ImGui layout: top bar (state, generation, FPS), log panel, instruction stream, kernel diff, neural weight heatmaps, and the memory block map. No scrollbars — every panel sizes to fit.

---

## Building

### 1. Bootstrap dependencies

```bash
bash scripts/setup.sh
```

This installs system packages, clones ImGui/wasm3, builds SDL3 from source (with per-file compile feedback), and builds Catch2 for tests. Pass `windows` to also cross-compile SDL3 for Windows via MinGW-w64.

```bash
bash scripts/setup.sh windows     # also builds Windows targets
bash scripts/setup.sh --clean     # wipe external/ and rebuild from scratch
```

### 2. Build

```bash
bash scripts/build.sh [TARGET]
```

| Target | Platform | Binary |
|---|---|---|
| linux-debug | Linux | build/linux-debug/bin/bootloader |
| linux-release | Linux | build/linux-release/bin/bootloader |
| windows-debug | Windows (MinGW) | build/windows-debug/bootloader.exe |
| windows-release | Windows (MinGW) | build/windows-release/bootloader.exe |

Default target is `linux-debug`. Pass `--clean` to wipe `build/` first.

---

## Running

```bash
bash scripts/run.sh           # build if needed, launch with GUI
bash scripts/run.sh --headless  # no window — FSM-only loop
bash scripts/run.sh --monitor   # background + tail logs
```

Keyboard shortcuts when the GUI is open:

| Key | Action |
|---|---|
| Space | Pause / resume |
| E | Force telemetry export now |
| F | Toggle fullscreen / windowed |
| H | Print shortcut help to log |
| Q / Esc | Quit |

---

## Command-line flags

| Flag | Default | Description |
|---|---|---|
| --gui / --no-gui | gui | Show window or run headless |
| --telemetry-level | full | none, basic, or full export detail |
| --telemetry-format | text | text or json |
| --telemetry-dir | auto | Override bin/seq root |
| --mutation-strategy | random | random, blacklist, or smart |
| --heuristic | none | none, blacklist, or decay |
| --max-gen | 0 (unlimited) | Stop after N successful generations |
| --max-run-ms | 0 (unlimited) | Watchdog: exit after N milliseconds |
| --max-exec-ms | 0 | Per-execution timeout (Unix only) |
| --save-model | — | Path to write model checkpoint on exit |
| --load-model | — | Path to load model checkpoint on start |
| --kernel | glob | glob (quine kernel) or seq (recurrent prototype) |
| --profile | off | Record per-generation timing stats |

Unknown flags emit a warning and are otherwise ignored, so wrapper scripts can safely forward extra arguments.

---

## Testing

```bash
bash scripts/test.sh
```

Builds the project, runs all `bin/test_*` binaries under the build directory, and prints a summary. Tests cover base64, WASM parsing, kernel boot, evolution, CLI parsing, telemetry export, advisor, feature extraction, loss, policy, trainer, and training phase transitions.

---

## Telemetry & checkpoints

Every successful generation writes:

- `bin/seq/<runid>/gen_<n>.txt` — human-readable report: header, mutation/trap stats, opcode list, hex dump, disassembly, history
- `bin/seq/<runid>/kernel_<n>.b64` — raw base64 kernel blob
- `bin/logs/*.log` — runtime log stream

After 50 generations, training completes and the model is saved to `bin/seq/model_checkpoint.dat`. On next start the checkpoint is auto-loaded if found there.

---

## Project layout

```
.
├── CMakeLists.txt          top-level build (C++17, Ninja)
├── src/
│   ├── core/               App, BootFsm, exporter, logger, constants
│   ├── nn/                 Trainer, Policy, Feature, Loss, Advisor
│   ├── wasm/               WasmKernel (wasm3 wrapper), parser, evolveBinary
│   └── gui/                Gui (window + panels), GuiHeatmap, colors
├── scripts/                setup.sh, build.sh, run.sh, test.sh
├── docs/                   architecture, design, specs, workflows
├── external/               populated by setup.sh (SDL3, ImGui, wasm3, Catch2)
└── cmake/                  toolchain files
```

---

## Known issues

- LSTM layer gates are not trained (dense layers only). Sequence-mode training feeds opcodes one at a time but the LSTM state update gradients are not back-propagated.
- JSON telemetry historically omitted a comma between some fields — parsers should be tolerant until this is addressed.
- Signal handler safety on some platforms: see `docs/PORTING_REPORT.md`.
