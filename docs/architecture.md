# Architecture Document — quine-grub-wasm

## Module Map

```
main.cpp
 └── Gui          (gui/window.h / gui/window.cpp)
      ├── GuiHeatmap    (gui/heatmap.h / gui/heatmap.cpp)
 │    └── [uses] gui/colors.h, util.h, wasm/parser.h
 └── App          (app.h / app.cpp)
      ├── BootFsm       (fsm.h / fsm.cpp)
      ├── AppLogger     (log.h / log.cpp)
      ├── WasmKernel    (wasm/kernel.h / wasm/kernel.cpp)
      ├── [uses] wasm/evolution.h / wasm/evolution.cpp
      ├── [uses] wasm/parser.h / wasm/parser.cpp
      ├── [uses] exporter.h / exporter.cpp
      └── [uses] base64.h, constants.h, types.h, util.h
```

---

## Source File Specifications

### `src/main.cpp`
**Role:** Entry point.

- Initialises SDL3 (video subsystem, window, renderer).
- Creates `Gui` and `App` instances.
- Runs the event loop: polls SDL events → `app.update()` → `gui.renderFrame(app)`.
- Destroys everything in reverse order on exit.

**Dependencies:** `app.h`, `gui/window.h`, `SDL3/SDL.h`, `backends/imgui_impl_sdl3.h`

---

### `src/types.h`
**Role:** Shared plain-old-data types used throughout the codebase.

| Symbol | Kind | Description |
|---|---|---|
| `SystemState` | enum class | FSM states: IDLE, BOOTING, LOADING_KERNEL, EXECUTING, VERIFYING_QUINE, SYSTEM_HALT, REPAIRING |
| `LogEntry` | struct | id, timestamp (ms), message, type string |
| `HistoryEntry` | struct | generation, ISO timestamp, size, action, details, success flag |
| `BootConfig` | struct | memorySizePages, autoReboot, rebootDelayMs |

**Dependencies:** `<string>`, `<cstdint>` — no project headers.

---

### `src/constants.h`
**Role:** Compile-time constants.

| Symbol | Description |
|---|---|
| `KERNEL_GLOB` | Base64-encoded minimal WASM binary (the initial quine kernel) |
| `KERNEL_SEQ`  | Base64-encoded tiny recurrent kernel that updates an internal hidden state and reports float weights to the host via `env.record_weight` — the prototype for the in-kernel autoregressive predictor |
| `DEFAULT_BOOT_CONFIG` | Default `BootConfig` values |

**Dependencies:** `types.h`

---

### `src/base64.h`
**Role:** Base64 encoding utilities (decode logic moved to `base64.cpp`).

A small companion file `src/base64.cpp` contains the decode implementation and static lookup table to keep the header light.

| Function | Signature | Description |
|---|---|---|
| `base64_encode` | `(const std::vector<uint8_t>&) → std::string` | Encode raw bytes |
| `base64_decode` | `(const std::string&) → std::vector<uint8_t>` | Decode base64 string; implementation in companion cpp file |

**Dependencies:** `<string>`, `<vector>`, `<cstdint>` — no project headers.

---

### `src/util.h` / `src/util.cpp`
**Role:** Cross-cutting string and time utilities.

| Function | Description |
|---|---|
| `stateStr(SystemState)` | Human-readable state name |
| `randomId()` | Generate a 9-character alphanumeric ID |
| `nowIso()` | Current UTC time as ISO-8601 string |

**Dependencies:** `types.h`

---

### `src/fsm.h` / `src/fsm.cpp`
**Role:** Finite State Machine governing the boot lifecycle.

| Member | Description |
|---|---|
| `transition(SystemState)` | Move to a new state; fires `OnTransition` callback |
| `current()` | Query the current state |
| `elapsedMs()` | Milliseconds since the last transition |
| `enteredAt()` | SDL tick timestamp of the last transition |
| `setTransitionCallback(cb)` | Register a state-change observer |

Transition history is not recorded here; that is the responsibility of `AppLogger`.

**Dependencies:** `types.h`, `SDL3/SDL.h`

---

### `src/logger.h` / `src/log.cpp`
**Role:** Append-only log ring-buffer and immutable history ledger.

| Member | Description |
|---|---|
| `log(msg, type)` | Append a `LogEntry`; deduplicates within 100 ms; caps at 1 000 entries.  Also callable via `App::log()` wrapper. |
| `addHistory(entry)` | Append a `HistoryEntry` (never truncated) |
| `logs()` | Read-only reference to the live log `std::deque` |
| `history()` | Read-only reference to the history `std::vector` |

**Dependencies:** `types.h`, `util.h`, `SDL3/SDL.h`

---

### `src/exporter.h` / `src/exporter.cpp`
**Role:** Generate a human-readable telemetry report.

| Symbol | Description |
|---|---|
| `ExportData` | POD aggregate: generation, kernel, instructions, logs, history |
| `buildReport(data)` | Returns a multi-section text string (header, hex dump, disassembly, history log) |

**Dependencies:** `types.h`, `wasm/parser.h`, `base64.h`, `util.h`

---

### `src/app.h` / `src/app.cpp`
**Role:** Top-level simulation orchestrator.

Coordinates `BootFsm`, `AppLogger`, `WasmKernel`, and the mutation engine.
Implements the boot sequence steps (`startBoot`, `tickBooting`, `tickLoading`,
`tickExecuting`, `tickVerifying`, `tickRepairing`, `doReboot`) and the WASM
host callbacks (`onWasmLog`, `onGrowMemory`).

Exposes read-only accessors for everything the `Gui` needs to render.

- **Kernel cache:** maintains `m_currentKernelBytes` and
  `m_instructions` via `updateKernelData()` so the same base64 string is
  only decoded once per change, reducing CPU overhead during tight
  update loops.
- **Training cycle:** every `kAutoTrainGen` successful generations the app
  pauses evolution, reloads telemetry into the `Advisor`, and enters a
  supervised training phase.  `Trainer::reset()` clears statistics and the
  replay buffer at the start of each cycle while leaving learned weights
  intact.  Once training completes the app writes a checkpoint file and
  returns to evolution.
- **UI logging helper:** provides `log(msg,type)` which simply forwards to
  the underlying `AppLogger` instance; this is used by the `main.cpp`
  shortcut handlers and is convenient for any component that has an
  `App&` reference.

**Dependencies:** `fsm.h`, `logger.h`, `exporter.h`, `wasm/kernel.h`, `wasm/parser.h`,
`wasm_evolution.h`, `base64.h`, `constants.h`, `util.h`, `types.h`

---

### `src/wasm/kernel.h` / `src/wasm/kernel.cpp`
**Role:** Wrapper around the wasm3 interpreter.

| Member | Description |
|---|---|
| `bootDynamic(b64, logCb, growCb)` | Decode kernel, instantiate wasm3 runtime, link host imports (supports optional `spawnCb`, `weightCb` and `killCb` callbacks).  `weightCb` is used by the sequence-model kernel to send learned weights back to the host. |
| `runDynamic(b64)` | Write base64 into WASM memory at offset 0, then call the exported `run(ptr, len)`.  Kernels without a memory section (e.g. KERNEL_SEQ) skip the memcpy and receive `run(0, 0)` directly. |
| `terminate()` | Free wasm3 runtime and environment |
| `isLoaded()` | True when a module is ready to execute |

`record_weight` is linked with signature `(i32, i32)` for KERNEL_GLOB and `(f32, f32)` for KERNEL_SEQ; `bootDynamic` tries both signatures automatically to avoid link errors.

Uses `m3ApiRawFunction` / `m3ApiGetArg` / `m3_LinkRawFunction` from wasm3.
`KernelUserData` (owned as `m_userData`) is passed as wasm3 user-data and
deleted in `terminate()`.

**Dependencies:** `types.h`, wasm3 headers, `base64.h`

---

### `src/wasm/parser.h` / `src/wasm/parser.cpp`
**Role:** Minimal WASM binary parser.

| Symbol | Description |
|---|---|
| `Instruction` | `{ opcode, args, originalOffset, length }` |
| `decodeLEB128(data, offset)` | Decode unsigned LEB-128 integer |
| `encodeLEB128(value)` | Encode integer as unsigned LEB-128 |
| `extractCodeSection(bytes)` | Parse and return the list of `Instruction`s from the code section |
| `parseInstructions(data, start, end)` | Parse a raw byte range into instructions |
| `getOpcodeName(opcode)` | Map WASM opcode byte to mnemonic string |

**Dependencies:** `<vector>`, `<cstdint>` — no project headers.

---

### `src/wasm/evolution.h` / `src/wasm/evolution.cpp`
**Role:** WASM binary mutation engine.

| Symbol | Description |
|---|---|
| `EvolutionResult` | `{ binary (base64), mutationSequence, description }` |
| `evolveBinary(b64, knownInstructions, seed, strategy)` | Apply one mutation to the code section; return a new base64 binary. `strategy` may be RANDOM, BLACKLIST or SMART to bias selection. The result is validated (magic/header, code parsing, trial boot) before acceptance; invalid candidates cause an `EvolutionException` with the failing base64 attached. Existing `call` instructions are left intact while any calls introduced by the mutation are stripped.  If the kernel implements the `env.record_weight` import (e.g. the `seq` prototype), evolveBinary will execute the candidate once and store any returned floats in `EvolutionResult::weightFeedback`, allowing the host to experiment with on-the-fly evaluation. |

Uses `std::mt19937` seeded from `std::random_device` for all random choices.

**Dependencies:** `wasm_parser.h`, `base64.h`

---

### `src/nn/policy.h` / `src/nn/policy.cpp`
**Role:** Feed-forward neural network policy with Dense and LSTM layers.

| Member | Description |
|---|---|
| `addDense(in, out)` | Append a fully-connected layer with ReLU activation.  Weights initialised with **Xavier-uniform** (`±√(6/(fan_in+fan_out))`) to break symmetry and avoid dead ReLUs from zero initialisation. |
| `addLSTM(in, hidden)` | Append an LSTM layer (4 gate weight matrices + biases, also Xavier-uniform init).  Hidden/cell state is updated in-place across calls and zeroed by `resetState()`. |
| `forward(input)` | Single forward pass; returns final layer output. |
| `forwardActivations(input, acts)` | Forward pass that records every layer's post-activation output in `acts`.  `acts[0]` = padded input; `acts[l+1]` = output of layer `l`.  Input is silently padded/truncated to the expected size so callers cannot cause out-of-bounds reads. |
| `backward(acts, outputGrad, lr, gradClip, weightDecay)` | Full chain-rule backpropagation.  Dense layers apply the ReLU derivative mask (`∂ReLU/∂z = 1 if output > 0 else 0`) when propagating deltas.  The LSTM performs single-step BPTT: gate gradients (`df`, `di`, `dg`, `do`) are derived from `dh` and the saved gate cache, then all four gate weight matrices are updated.  Every weight step is clipped to `±gradClip` and followed by L2 weight decay. |
| `resetState()` | Zero LSTM hidden/cell state — call before processing a new sequence. |
| `forwardSequence(seq)` | Reset state, then apply `forward()` for each step in `seq`; return the final output. |
| `layerLstmH/C(i)` | Read LSTM hidden/cell state for layer `i` (checkpoint persistence). |
| `setLayerLstmH/C(i, v)` | Restore LSTM state from a checkpoint. |

**LSTMCache** (nested in `Layer`) stores the forward-pass gate values needed for BPTT: `xh` (concatenated input + hidden), `f/i_gate/g/o` (post-activation values), and `c_prev` (cell state before step).

**Dependencies:** `<vector>`, `<cmath>` — no project headers.

---

### `src/nn/train.h` / `src/nn/train.cpp`
**Role:** Online SGD trainer that wraps the policy network.

| Member | Description |
|---|---|
| `observe(entry)` | Extract features, run `forwardActivations`, compute training target (normalised reward − quality penalty from `Loss::compute()`), call `policy.backward()`, update EMA loss stats, optionally sample from the replay buffer for an extra update. |
| `save(path)` | Persist weights, biases, LSTM state, and training stats to a plain-text file. |
| `load(path)` | Restore from a checkpoint written by `save()`. |
| `reset()` | Clear observations, EMA loss, and replay buffer without touching weights. |

Hyper-parameters (constants in `train.h`): `kLearningRate = 0.004`, `kEmaDecay = 0.90`, `kGradClip = 0.5`, `kWeightDecay = 2e-5`.

**Dependencies:** `nn/policy.h`, `nn/advisor.h`, `nn/feature.h`, `nn/loss.h`

---

### `src/nn/feature.h` / `src/nn/feature.cpp`
**Role:** Extract a fixed-length feature vector from a `TelemetryEntry`.

| Slot range | Content |
|---|---|
| 0–255 | Normalised opcode histogram (one bucket per WASM opcode) |
| 256–511 | Bigram counts (pairs of consecutive opcodes) |
| 512–523 | Structural metadata (trap flag, generation, drop ratio, sequence length, …) |
| 640–1023 | Positional opcode sequence (first 384 opcodes, zero-padded) |

Total: `kFeatSize = 1024`.

**Dependencies:** `nn/advisor.h`, `wasm/parser.h`, `base64.h`

---

### `src/nn/loss.h` / `src/nn/loss.cpp`
**Role:** Scalar loss function for the trainer.

`Loss::compute(entry)` returns a **badness score** (lower is better; negative means the kernel is doing well).  Components:

| Signal | Effect |
|---|---|
| `-generation` | Primary reward: more generations = lower loss |
| Trap penalty | +8.0 when `trapCode` is non-empty |
| Drop-ratio penalty | Proportional penalty when >40 % of instructions are `drop` (0x1A) |
| Diversity penalty | Proportional penalty when unique-opcode count is below 64 |
| Short-sequence penalty | +0.5 per instruction below 8 |
| Long-sequence reward | −up to 2.0 for sequences longer than 32 instructions |

**Dependencies:** `nn/advisor.h`, `wasm/parser.h`, `base64.h`

---

### `src/nn/advisor.h` / `src/nn/advisor.cpp`
**Role:** Telemetry store and heuristic scoring oracle.

`Advisor` scans a telemetry directory tree and loads `TelemetryEntry` records from exported report files.  `score(opcodeSequence)` returns a value in [0, 1] based on the average generation of loaded entries and the Jaccard-style similarity of the query sequence to known high-fitness sequences.

**Dependencies:** `types.h`, `wasm/parser.h`, `base64.h`

---
**Role:** Dear ImGui backend lifecycle and panel orchestration.

`Gui` creates the ImGui context in `init()`, renders all panels in
`renderFrame()`, and tears everything down in `shutdown()`.

The application writes runtime logs via `AppLogger` to a file; the path is
relative to the current working directory, which `run.sh` sets to the
build target folder.  Logs therefore appear under `bin/logs/`.  `run.sh`
adds a bit of user feedback when invoked: it prints the command line being
executed, the paths for logs and telemetry, and after a run it reports the
exit code and tails the end of any log files so the user can see what
happened without opening them manually.

For any transient artifacts (pipe files, intermediate logs, etc.) the
agent and scripts should create a `./.tmp` directory at the repo root
instead of relying on `/tmp`.  This keeps all temporary data scoped to the
project.
also provides a `--monitor` mode to tail these files while the program
runs.

A DPI scaling factor is computed during `init()` based on the SDL window
resolution using `computeDpiScale(SDL_Window*)` from `util.cpp`.  The raw scale
is stored in `m_dpiScale` and then multiplied by an additional UI boost factor
(≈1.5) to yield `m_uiScale`, which is the value actually applied to
`io.FontGlobalScale` and used when sizing buttons and panels.  This ensures a
large, touch‑friendly interface even on modest displays.  The boosted value is
exposed via `uiScale()`, while the raw DPI value is still accessible via
`dpiScale()`; both are tested.

The ImGui style is also modified at init to favour dark backgrounds with neon
accents (purples, cyans) and increased frame padding/item spacing, giving the
application a sci‑fi / cyberpunk visual feel without becoming gimmicky.

Panel helpers:

| Method | Panel |
|---|---|
| `renderTopBar` | GEN / STATE / UPTIME / control buttons |
| `renderLogPanel` | System log ring-buffer |
| `renderInstrPanel` | WASM instruction list with IP highlight |
| `renderKernelPanel` | Base64 kernel diff viewer |
| `renderInstancesPanel` | Shows all spawned kernels with kill buttons and basic telemetry |
| `renderStatusBar` | Bottom status line |
| `renderWeightHeatmaps` | Draw per-layer policy weight grids above memory panel.  To avoid bogging down the evolution scene, the matrices are rasterised into SDL textures once per generation and merely blitted each frame; legacy fallback code remains for sanity. |

Owns a `GuiHeatmap` instance (`m_heatmap`) which renders the memory panel.

**Dependencies:** `app.h`, `gui/heatmap.h`, `gui/colors.h`, `util.h`, `wasm_parser.h`

---

### `src/colors.h`
**Role:** Header-only colour lookup table.

Inline functions mapping `SystemState` and log-type strings to
`ImVec4` RGBA colours.  No `.cpp` companion needed.

| Function | Description |
|---|---|
| `colorForLogType(type)` | Log entry accent colour |
| `colorForState(state)` | State badge colour |

**Dependencies:** `types.h`, `imgui.h`

---

### `src/heatmap.h` / `src/heatmap.cpp`
**Role:** Memory heatmap visualizer.

`GuiHeatmap` maintains a `std::vector<float>` of per-block heat values that
decay each frame. `draw()` renders into an `ImDrawList`; `renderPanel()` renders
the full panel (header + background + heatmap).

Block geometry scales automatically with kernel size:

| Kernel size | Block px | Bytes/block |
|---|---|---|
| < 256 B | 8 | 1 |
| < 1 KB | 5 | 4 |
| ≥ 1 KB | 3 | 16 |

**Dependencies:** `app.h`, `gui/colors.h`, `util.h`, `imgui.h`

---

## Dependency Graph (simplified)

```
types.h ◄── constants.h
   ▲               ▲
   │               │
base64.h    wasm/parser.h ◄── wasm/evolution.h
   ▲               ▲                  ▲
   │               │                  │
util.h ────────────┤           kernel.h
   ▲               │                  ▲
   │               │                  │
log.h     exporter.h          fsm.h
   ▲               ▲                  ▲
   └───────────────┴──── app.h ───────┘
                              ▲
                              │
             gui/colors.h ─── gui/window.h ─── gui/heatmap.h
                              ▲
                              │
                          main.cpp
```

---

## Build System

| File | Role |
|---|---|
| `CMakeLists.txt` | Single `add_executable(bootloader …)` target; C++17; links SDL3::SDL3 statically |
| `scripts/setup.sh` | Installs apt packages, clones external libs, builds SDL3, builds the project |
| `scripts/build.sh` | Configures + compiles one of four targets; `--clean` wipes build/ |
| `scripts/run.sh` | Builds if needed, then execs the binary |
| `cmake/toolchain-windows-x64.cmake` | CMake toolchain for MinGW-w64 Windows cross-compile from WSL |

External libraries live in `external/` (not committed; populated by `setup.sh`):

| Library | Path | Purpose |
|---|---|---|
| SDL3 (source) | `external/SDL3/src/` | Shared SDL3 source tree |
| SDL3 (Linux)  | `external/SDL3/linux/` | Linux build |
| SDL3 (Windows)| `external/SDL3/windows/` | Windows cross-compile (optional) |
| Dear ImGui | `external/imgui/` | Immediate-mode GUI |
| wasm3 | `external/wasm3/` | WebAssembly interpreter |
