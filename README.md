# WASM Quine Bootloader

This project now supports configurable mutation heuristics and richer
telemetry output.  See `docs/specs/spec_heuristics.md` and
`docs/specs/spec_telemetry.md` for details.  CLI flags such as
`--telemetry-level`, `--mutation-strategy`, `--profile` and
`--max-gen` allow fine‑grained control of runs.  Under the hood the
application caches the decoded kernel bytes and instruction stream to
avoid repeated base64 work, and the build system enforces
`-Werror` so the codebase must remain warning-free.  The CLI also
prints stderr warnings when a flag value is unrecognised, setting a
`parseError` flag that wrapper scripts or tests can inspect.  DPI
scaling is computed automatically from the window size and is clamped
to a maximum of **2×** to prevent runaway fonts on enormous displays.

A self-replicating, self-evolving WebAssembly kernel visualizer — native **C++17** desktop application using **SDL3** and **Dear ImGui**.

For technical design details see **[docs/design.md](docs/design.md)** and **[docs/architecture.md](docs/architecture.md)**.

Specification documents live under `docs/specs/` (e.g. CLI, telemetry formats).

---

## Features

| Feature | Description |
|---|---|
| WASM Quine | A minimal WebAssembly binary that echoes its own source back through a host import |
| Self-Evolution | Binary mutates each generation (insert / delete / modify / append), sanity-checking each candidate by loading and running it before acceptance; new mutations are prevented from inserting CALL opcodes, while existing imports (e.g. `env.log`) are retained so host interactions continue |
| Memory Visualizer | Real-time SDL3 canvas heatmap of WASM heap activity |
| Neural Weight Heatmaps | Per-layer color grids showing policy network weights (red positive / blue negative); displayed above the heap map in evolution mode.  To keep the evolution UI silky, the matrices are rasterised into textures once per generation instead of being redrawn each frame. |
| Instruction Stream | Step-by-step WASM opcode visualizer with program counter |
| Terminal Log | Colour-coded system event log (info / success / warning / error / mutation) |
| Era System | *removed; terminal-only app no longer uses eras* |
| Telemetry Export | Dump full hex / disassembly / history report to a `.txt` file |
| Kernel Cache | App retains decoded kernel/instruction data between generations to
  reduce CPU work |
| DPI Scaling & Touch UI | UI text and widgets automatically scale with window size (1×–2×); a light boost (~0.65× the raw DPI factor) is applied for readability, especially on touch screens. |
| Multi-Instance Support | Kernels can `env.spawn` siblings; instances are tracked, exported
  in telemetry, and a GUI panel lets you inspect or kill them |
| Training UI Clarity | Initial training screen now displays the exact telemetry
  directory being scanned (build/<target>/bin/seq) to avoid confusion when
  running from different working directories |

---

## Agent Skills

This repo includes a full suite of Copilot agent skills; see `.github/copilot-instructions.md` for a current list.  Key skills include:

- `setup-project`, `build-app`, `run-app`, `test-app`
- `update-docs`, `update-specs`, `update-skills`, `update-memory`
- `search-memory`, `telemetry-review`, `introspect-telemetry`
- `repo-facts` – deep dive into the repository and report its architecture, design and blockers.
- `improve-skills`, `improve-src`, `commit-push`
- `update-issues`  <!-- handles scanning open issues, closing implemented ones, and generating new tasks from code reviews -->

Agents can invoke them via natural language prompts; the skills and
instructions are maintained by the `update-skills`/`improve-skills` tools.
When told to "run" a skill the agent should execute its logic and make the
corresponding edits automatically rather than simply listing what could be
done.

Key skills now also include `code-review` for comprehensive reviews,
`improve-tests` for expanding the unit test suite, and `timed-run` for
shortcut experiment cycles with automatic telemetry analysis.  Workflow
prompts are stored under `.github/prompts/` and guide agent behaviour for
the master and development workflows.  The development prompt now also
offers an optional issue‑triage step: if the backlog is quiet it will run
`code-review` to produce new task candidates.

---

## Project Layout

```
.
├── CMakeLists.txt            # CMake build definition (C++17, Ninja)
├── src/                      # C++17 application source
│   ├── main.cpp              # SDL3 init + main loop
│   ├── gui/window.h / gui/window.cpp # Gui class: ImGui backend lifecycle, panel orchestration
│   ├── gui/colors.h          # Header-only colour helpers (state/log → ImVec4)
│   ├── gui/heatmap.h/.cpp    # GuiHeatmap: memory heat-decay visualizer
│   ├── util.h / util.cpp     # stateStr, randomId, nowIso
│   ├── app.h / app.cpp       # App: top-level orchestrator
│   ├── fsm.h / fsm.cpp       # BootFsm: finite state machine
│   ├── log.h / log.cpp # AppLogger: live log ring-buffer + history ledger
│   ├── exporter.h / exporter.cpp # buildReport(): telemetry text report
│   ├── types.h               # SystemState, LogEntry, HistoryEntry, BootConfig
│   ├── constants.h           # KERNEL_GLOB (base64 WASM), DEFAULT_BOOT_CONFIG
│   ├── base64.h              # Base64 encode utilities (decode implementation in base64.cpp)
│   ├── wasm/                  # subdirectory for WASM-related modules
│   │   ├── parser.h/.cpp    # WASM binary parser (LEB-128, code section)
│   │   ├── evolution.h/.cpp # WASM mutation engine
│   │   └── kernel.h/.cpp    # WasmKernel – wasm3 integration
├── scripts/
│   ├── setup.sh              # One-shot dependency installer + initial build
│   ├── build.sh              # Build for a specific target (or --clean)
│   └── run.sh                # Build if needed, then launch
├── cmake/
│   └── toolchain-windows-x64.cmake  # MinGW-w64 CMake toolchain (Windows cross-compile)
├── docs/
│   ├── design.md             # Design goals, simulation loop, mutation strategy
│   └── architecture.md       # Per-file module specs and dependency graph
└── external/                 # Populated by scripts/setup.sh (NOT committed)
    ├── imgui/
    ├── wasm3/
    └── SDL3/
        ├── src/              # SDL3 source tree
        ├── linux/            # Built + installed for Linux
        └── windows/          # Built + installed for Windows (optional)
```

> **Note:** `external/` is excluded from the repository (`.gitignore: /external/*`).
> Run `bash scripts/setup.sh` to populate it.

---

## Quick Start (Windows WSL2 / Ubuntu)

### Step 1 — Install dependencies and build

> **Note:** the CMake project now enables `-Werror` on GCC/Clang so any
> compiler warnings will be treated as errors.  Fix warnings before
> building or use `bash scripts/build.sh --clean` to reset.

The `setup.sh` script can also reset your workspace:

```bash
bash scripts/setup.sh --clean    # remove external/, bin/ and then run setup
```

This is useful if you want a completely fresh third-party checkout before
re-running the normal install steps.

```bash
bash scripts/setup.sh
```

By default the build scripts will pick up whatever C and C++ compilers are first on your `PATH` (for example `/usr/bin/gcc`/`/usr/bin/g++`).
If you are running under WSL and have a non‑default compiler installed (clang, a different gcc version, etc.), you can force the project to use it by exporting the standard environment variables **before** invoking `build.sh`:

```bash
# use g++-12 instead of the system default
export CC=/usr/bin/gcc-12
export CXX=/usr/bin/g++-12
bash scripts/build.sh
```

The `build.sh` helper now passes those overrides through to `cmake`; you can also pass them manually via `-DCMAKE_C_COMPILER`/`-DCMAKE_CXX_COMPILER` if you prefer.

This script will:
1. Install system packages (`build-essential`, `cmake`, `ninja-build`, SDL3 system deps, fonts)
2. Clone **Dear ImGui** → `external/imgui`
3. Clone **wasm3** → `external/wasm3`
4. Install or build **SDL3** for Linux → `external/SDL3/linux`
5. Build the `linux-debug` target → `build/linux-debug/bin/bootloader`

### Step 2 — Run

> **NOTE:** `scripts/run.sh` was recently fixed; it now correctly invokes the
> `BINARY` variable after changing directories.  Older versions assumed `./bootloader`
> which could fail if you started the script from a different working directory.


Keyboard shortcuts are available when using the GUI version:

- **Spacebar** – pause or resume the current kernel.
- **E** – export the current kernel/telemetry immediately.
- **F** – toggle fullscreen/windowed (useful when switching focus).
- **H** – log a help message summarising shortcuts.
- **Q** or **Escape** – quit the application.

The launcher also supports several command‑line options (see
`spec_cli.md` for full semantics):

- `--telemetry-level=<none|basic|full>` – control how much export data is
  written (header only, full report, or disabled).  The default is now
  **full** so that every run generates a useful history export without
  extra flags.
- `--telemetry-dir=<path>` – change output directory for reports.
- `--telemetry-format=<text|json>` – choose the export file format; JSON is
  easier for scripts to parse.
- `--mutation-strategy=<random|blacklist|smart>` – choose evolution
  policy; `blacklist` enables the adaptive heuristic.
- `--heuristic=<none|blacklist|decay>` – shorthand toggle for the heuristic;
  `decay` mode will gradually forget entries after each successful
  generation.
- `--profile` – log per-generation timing and memory usage.
- `--max-gen=<n>` – stop after `n` successful generations (handy for CI).
- `--max-run-ms=<n>` – exit once the bootloader has been running for roughly `n` milliseconds; acts as a simple watchdog for long‑running jobs.
- `--max-exec-ms=<n>` – limit each WASM kernel execution to roughly `n` milliseconds; kernels that overrun are killed and flagged as failures (Unix only).
- `--save-model=<path>` / `--load-model=<path>` – persist or restore the
  trainer model between runs (used by `train` and related utilities).

Unrecognised flag values (e.g. `--telemetry-level=foo`) produce a warning
on stderr but do not abort execution; the parser sets a `parseError`
flag that callers may inspect.

These may be passed to `scripts/run.sh` and will be forwarded to the
binary when launched directly.
A helper script `scripts/telemetry_analysis.py` can parse generated
telemetry exports (`gen_*.txt`) and summarise metrics such as mutation
rates, generation durations, and instance counts.  Run it directly or
integrate it into research workflows.
```bash
# GUI is the default; window starts fullscreen
bash scripts/run.sh            # equivalent to --gui

# force headless/terminal mode (no SDL3 window)
bash scripts/run.sh --headless  # also --no-gui / --nogui

# request a windowed UI instead of the fullscreen default
bash scripts/run.sh --windowed

# run and tail logs
bash scripts/run.sh --monitor

The `run.sh` wrapper now prints the full command line it will execute along
with the paths where logs and telemetry will be written.  After a
headless run it also reports the process exit code and shows the last 20
lines of any generated log files, giving immediate feedback without
manually opening files.
```

By default the executable is started with its working directory set to the
build target (e.g. `build/linux-debug`).  However the **actual base path for
logs and telemetry is computed from the executable’s location** so that
running the binary from another directory (including the repo root) still
writes into the build tree.  Files appear under
`<exe_dir>/bin/logs/` and `<exe_dir>/bin/seq/<runid>/` where `<exe_dir>` is
where the bootloader executable lives.  The wrapper script (`run.sh`) changes
into the build directory merely for convenience and to mirror prior
behaviour, but the app no longer depends on the working directory.

The build script prints colored, prefixed `[build]` messages (green for info,
yellow for warnings, red for errors) on top of the usual cmake/ninja output to
make important steps easy to scan.

The test script (`test.sh`) now concludes with a colored summary box
showing total executables run, cumulative assertions and wall‑clock
seconds.  This makes it easy to see at a glance whether the test suite
passed and how long it took.

The `--monitor` option runs the bootloader in the background and tails
`bin/logs/*.log` in real time so you can watch system messages as evolution
proceeds.

### Cleanup

`bash scripts/build.sh --clean` removes the entire `build/` directory along
with CMake/Ninja caches.  It also deletes any generated `bin/` hierarchy at the
repo root, wiping old logs, sequence exports or temporary files produced by
runs or tests.  This leaves the source tree pristine.

---
---

## Known issues & limitations

* The JSON telemetry exporter currently emits malformed syntax (missing comma)
  between the `heuristicBlacklistCount` and `advisorEntryCount` fields.  A fix
  is planned under issue #87.
* The `Trainer` class is still a stub; no actual learning occurs yet.  Training
  weights are saved/loaded as simple counters.  See issue #89 for details.
* Logs are not written to `bin/logs` automatically; all telemetry lives in the
  `seq` folder and can be parsed by `telemetry_analysis.py`.

These items will be addressed in upcoming releases; they do not prevent basic
functionality but are worth keeping in mind for long experiments.
## Build Targets

| Target | Platform | Build type | Output binary |
|---|---|---|---|
| `linux-debug`    | Linux | Debug   | `build/linux-debug/bin/bootloader` |
| `linux-release`  | Linux | Release | `build/linux-release/bootloader` |
| `windows-debug`  | Windows (MinGW) | Debug   | `build/windows-debug/bootloader.exe` |
| `windows-release`| Windows (MinGW) | Release | `build/windows-release/bootloader.exe` |

```bash
bash scripts/build.sh                    # linux-debug (default)
bash scripts/build.sh linux-release
bash scripts/build.sh windows-debug     # requires: bash scripts/setup.sh windows
bash scripts/build.sh windows-release   # requires: bash scripts/setup.sh windows
bash scripts/build.sh --clean           # remove build/ dir + caches
bash scripts/build.sh --clean linux-debug   # clean then build linux-debug
```
### Windows cross-compile setup

```bash
bash scripts/setup.sh windows   # installs MinGW-w64 + builds SDL3 for Windows
bash scripts/build.sh windows-release
```

---

## Running the Bootloader

```bash
# GUI by default (linux-debug)
bash scripts/run.sh

# Specific build target
bash scripts/run.sh linux-release

# Headless mode (no window)
bash scripts/run.sh --headless

# Direct executable (after build)
./build/linux-debug/bin/bootloader
```

### Runtime keyboard controls

| Key / Button | Action |
|---|---|
| `Space` | Pause / Resume the simulation |
| `Q` / `Esc` | Quit |
| **PAUSE SYSTEM** button | Same as Space |
| **RESUME SYSTEM** button | Same as Space (shown when paused) |
| **EXPORT** button | Write telemetry report → `quine_telemetry_gen<N>.txt` |
| **COPY** button | Copy current kernel base64 to clipboard |

### HUD panels

| Panel | Description |
|---|---|
| **Top bar** | GEN · STATE · UPTIME · RETRIES – control buttons |
| **System Log** | Colour-coded ring-buffer of up to 1 000 events; auto-scrolls |
| **Instruction Stack** | WASM opcode list with live program-counter highlight |
| **Kernel Source** | Base64 diff view: header (blue), mutation (yellow), expansion (green) |
| **Memory Map** | Heat-decay block visualizer of WASM linear memory activity |
| **Status Bar** | RUNNING/PAUSED indicator |
| **Instances Panel** | List of spawned kernels with kill buttons (when N>0) |

---

## Simulation States (FSM)

```
IDLE ──▶ BOOTING ──▶ LOADING_KERNEL ──▶ EXECUTING
                                              │          │
                                       VERIFYING_QUINE  │
                                              │          ▼
                                           (reboot)  REPAIRING
                                              │          │
                                              └────┬─────┘
                                                   ▼
                                                 IDLE
```

| State | Colour | Description |
|---|---|---|
| IDLE | — | Waiting; immediately transitions to BOOTING |
| BOOTING | 🟡 yellow | Brief delay; scales with generation |
| LOADING_KERNEL | 🔵 blue | Byte-by-byte kernel loading animation |
| EXECUTING | 🟢 green | WASM kernel running; instruction-step visualizer active |
| VERIFYING_QUINE | 🟣 purple | Quine check passed; brief hold before reboot |
| REPAIRING | 🟠 orange | Quine check failed; adaptive mutation + retry |
| SYSTEM_HALT | 🔴 red | Unrecoverable error |

---

## Dependencies

| Library | Version | Purpose |
|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | 3.2.x | Window, renderer, events |
| [Dear ImGui](https://github.com/ocornut/imgui) | master | Immediate-mode GUI |
| [wasm3](https://github.com/wasm3/wasm3) | main | WebAssembly interpreter |

All three are fetched by `scripts/setup.sh`.


## Original TypeScript Web App

## Original TypeScript Web App (archived)

The original TypeScript/React reference implementation was previously stored in `web/` but has been removed from this repository after a successful native C++ port. If you need the original prototype, consult the repository history or contact the maintainers.
