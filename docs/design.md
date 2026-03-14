# Design Document — WASM Quine Bootloader

## 1. Goals

The project explores **self-referential computation** by combining two concepts:

1. A **WASM quine** — a WebAssembly binary whose only job is to write its own
   source representation into a host buffer and call a log import.
2. A **genetic mutation engine** — each successful quine cycle produces a
   slightly mutated descendant binary that is then validated and run.

The desktop application makes the simulation **observable in real time** via a
multi-panel Dear ImGui HUD rendered through SDL3.

---

## 2. Simulation Loop

Each frame (≈ 60 Hz) the `App::update()` drives the `BootFsm`:

```
Frame N
│
├─ BootFsm::current() == IDLE
│     → App::startBoot()         (transition → BOOTING)
│
├─ BootFsm::current() == BOOTING
│     → wait bootSpeed ms        (scales down with generation)
│     → transition → LOADING_KERNEL
│
├─ BootFsm::current() == LOADING_KERNEL
│     → advance loading progress LOAD_STEP bytes/frame
│     → when done: WasmKernel::bootDynamic()
│     → transition → EXECUTING
│
├─ BootFsm::current() == EXECUTING
│     → step through instructions at stepSpeed ms/instr
│     → after the final instruction has been stepped, the host calls
│       `WasmKernel::runDynamic()` once to exercise the current kernel.
│       (legacy versions looked for a `call` opcode as a trigger, which
│       proved fragile when mutations inserted spurious calls.)
│       ↳ host env.log callback → App::onWasmLog()
│           ├─ output == kernel?  → transition → VERIFYING_QUINE
│           └─ mismatch          → handleBootFailure()
│       ↳ host env.grow_memory   → App::onGrowMemory()
│
├─ BootFsm::current() == VERIFYING_QUINE
│     → wait rebootDelayMs (2 s default)
│     → doReboot(success=true)   (generation++, apply mutation)
│     → transition → IDLE
│
├─ BootFsm::current() == REPAIRING
│     → wait 1 500 ms
│     → doReboot(success=false)  (revert to stable kernel)
│     → transition → IDLE
│
└─ BootFsm::current() == SYSTEM_HALT
      → no-op
```

---

## 3. Mutation Strategy

On each successful quine execution `evolveBinary()` is called (now with a
`strategy` parameter that selects `RANDOM`, `BLACKLIST` or `SMART` behaviour).
It operates exclusively on the **code section** of the WASM binary and applies
one of four
mutation types, chosen probabilistically:

| Mutation type | Probability | Description |
|---|---|---|
| **Insert** | ~30 % | Splice a random safe instruction into the code section |
| **Delete** | ~20 % | Remove a random non-essential instruction |
| **Modify** | ~30 % | Replace a random instruction with a semantically similar one |
| **Append** | ~20 % | Append one or more instructions from the known-good pool |

Known-good mutation sequences accumulate over generations and are fed back into
the `append` path to bias later evolutions toward proven expansions.

After mutation the binary is base-64 encoded and validated by checking:
- WASM magic bytes `\0asm` at offset 0
- Successful re-parse by `extractCodeSection()`
- Instantiation with `WasmKernel::bootDynamic()` followed by a
  no-op `runDynamic("")` to ensure the module loads and its
  `run` export is callable without trapping.

Any mutation that fails these sanity checks throws an exception
(implemented as `EvolutionException`).  The exception object includes the
base64-encoded candidate so callers or telemetry can examine what was
rejected.  The calling code catches the exception and treats the candidate
as rejected, falling back to the previously stable kernel (or rolling
another mutation if inside a repair cycle).

* **Auto‑training trigger:** additionally the host monitors how many
  successful generations have been executed; once **50 generations** are
  reached the FSM pauses evolution, reloads the freshly written telemetry
  data, and enters the startup training phase automatically.  This removes
  manual mode switching and keeps training focussed on the most recent
  evolutionary run.

---

---

## 5. GUI Rendering and DPI Scaling

Throughout the evolution scene the HUD shows several panels: a system log,
instruction stream, kernel viewer, optional advisor/instance panels and
at the bottom a memory heatmap.  A new addition is a set of **neural weight
heatmaps** positioned immediately above the heap visualizer; each layer of
the policy network is rendered as a small grid of colored squares (red for
positive weights, blue for negative) and updates in real time as training
proceeds.  To prevent the evolution scene from stalling, the panel rasterises
each layer into an SDL texture only when the network weights change (typically
once per generation) and then simply blits the cached image every frame.



## 7. Additional Behavioural Enhancements

Several smaller usability improvements have been added in recent
revisions:

* **Kernel caching:** To reduce CPU overhead the `App` caches the
decoded bytes of the current base64 kernel and the corresponding
instruction list.  `updateKernelData()` is invoked whenever the
base64 string changes, preventing repeated decoding during tight
event/update loops or telemetry export.
* **CLI parsing warnings:** `parseCli()` now emits a warning to
stderr and sets a `parseError` flag whenever a flag value is
unrecognised (e.g. `--telemetry-level=frobnitz`).  Unknown options
are still ignored so wrapper scripts can forward extra arguments.
* **Log API convenience:** `App` exposes a simple `log(msg,type)`
wrapper that forwards to its `AppLogger`, making it easy for UI
handlers or shortcuts (see `main.cpp`) to append entries without
reaching into the logger object.
* **CALL handling:** The evolution engine no longer strips every
  `call` from the kernel; only instructions introduced by mutation are
  filtered.  Keeping the original calls preserves stack balance (previously
  we saw repeated validation failures when log invocations were replaced by
  NOPs).  New genomes are still sanitized to avoid references to nonexistent
  functions.
  opcodes from mutated kernels and the host ignores CALL instructions when
  executing; this prevents crashes caused by calls pointing at nonexistent
  functions and decouples the quine trigger from instruction semantics.
code computes a simple scale factor at startup that is proportional to the
window's pixel area (larger windows → bigger DPI).  The helper
`computeDpiScale(SDL_Window*)` lives in `src/util.cpp` and defaults to 1.0
(its return value is clamped to the range [1.0, 2.0] so extremely large
windows do not produce unreasonably huge fonts).

The `Gui` class calls this helper during `init()` and applies the result to
`ImGui::GetIO().FontGlobalScale`.  For a more touch‑friendly interface we
further boost the raw DPI value by a constant (currently **×1.5**, clamped
>=1) and store this as `m_uiScale`; `Gui::uiScale()` returns the boosted value
which is also used when sizing buttons and panels.

Telemetry exports are saved automatically each generation to
`bin/seq/<runid>/gen_<n>.txt` (plus `kernel_<n>.b64`) located under the
executable directory rather than the current working directory; this avoids
accidentally creating a `bin/` folder when launching from elsewhere.
directory.  Runtime logs are written to `bin/logs/` and can be monitored live
using `bash scripts/run.sh --monitor`.

Colors and padding are customised in `Gui::init()` to give a dark,
neon‑accented “cyberpunk” aesthetic: deep purples/teals for buttons and headers
on a near‑black background, with generous padding and item spacing.  The
goal is a snappy, readable, sci‑fi style UI that works well with fingers or a
mouse.

Scaling is intentionally coarse — the goal is readable fonts and big targets,
not pixel‑perfect retina support — which keeps the implementation simple and
avoids the need to recompute metrics on every frame.

---

## 6. WASM Host Imports

The kernel module imports two host functions from the `env` namespace:

| Import | Signature | Behaviour |
|---|---|---|
| `env.log` | `(i32 ptr, i32 len) → void` | Read `len` bytes from WASM memory at `ptr`; compare to current kernel base64 |
| `env.grow_memory` | `(i32 pages) → void` | Flash the memory-growing indicator for 800 ms |

---

## 6. Telemetry Export

Pressing **EXPORT** writes `quine_telemetry_gen<N>.txt` containing:

- Header (generation, kernel size, timestamp)
- Current kernel in base64
- Full hex dump of the raw WASM bytes
- Disassembly listing (index, address, opcode, args)
- History log (every EXECUTE / EVOLVE / REPAIR event)
