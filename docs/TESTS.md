# Unit Tests

Tests live in `test/` and are written with **Catch2 v3**.  Run them with:

```bash
bash scripts/test.sh
```

That script builds the project (if needed) and runs `ctest` to execute every registered test target.

---

## Test Suites

| File | Binary | Description |
|------|--------|-------------|
| `test_state.cpp` | `test_state` | `stateStr()` for every `SystemState` value; `BootFsm` transitions, callbacks, no-op guard; `randomId` format/uniqueness; `nowIso` / `nowFileStamp` format; `sanitizeRelativePath` security checks; `decodeBase64Cached` result stability |
| `test_base64.cpp` | `test_base64` | Base64 encode/decode round-trips, edge cases (empty input, padding) |
| `test_wasm.cpp` | `test_wasm` | LEB128 encode/decode round-trip; `parseInstructions` for simple and multi-byte sequences; `extractCodeSection` on real kernel blobs; KERNEL_SEQ weight callback; `WasmKernel` error paths |
| `test_util_dpi.cpp` | `test_util_dpi` | `computeDpiScale` for various window sizes; GUI DPI/UI scale application on init; automatic export file generation |
| `test_cli.cpp` | `test_cli` | `parseCli` defaults; `--headless` / `--windowed` / `--fullscreen`; `--telemetry-level`, `--max-gen`, `--profile`; mutation strategy / heuristic flags; unknown-flag error; numeric parse errors; `--telemetry-dir`, `--telemetry-format`; `--kernel` selection; `--max-exec-ms` / `--max-run-ms`; `--save-model` / `--load-model` |
| `test_cli_behavior.cpp` | `test_cli_behavior` | Higher-level CLI behaviour integration (headless mode, option combinations) |
| `test_export.cpp` | `test_export` | `buildReport` output includes all telemetry metrics, mutation breakdown, instances, opcode sequence section |
| `test_app.cpp` | `test_app_logic` | `App` FSM lifecycle, checkpoint saving, telemetry accumulation, observer callbacks |
| `test_evolution.cpp` | `test_evolution` | `evolveBinary` produces valid WASM magic header; outputs are bootable and runnable; no CALL opcodes in mutation sequence; handles large sequences, missing code sections, truncated bodies; regression: known-bad kernel traps |
| `test_advisor.cpp` | `test_advisor` | `Advisor` loads entries from telemetry files; scoring formula (avg / (avg+10)); exact sequence match → 1.0; empty advisor → 1.0; manual entry injection |
| `test_feature.cpp` | `test_feature` | Feature vector is `kFeatSize` long; opcode histogram from real kernel blob; trap-flag slot; `extractSequence` on valid/invalid WASM |
| `test_loss.cpp` | `test_loss` | Negative-generation primary signal; monotonic decrease with generation; trap penalty (+8); drop-ratio penalty (>40%); opcode diversity penalty; length penalty (short) and reward (long); combined signal ordering |
| `test_policy.cpp` | `test_policy` | Dense forward pass with known weights; Xavier-uniform init (non-zero, within bounds) for dense and LSTM layers; `forwardActivations` layer/size structure; undersized input padding safety; `backward()` changes weights; gradient clipping correctness; LSTM receives backprop; LSTM hidden/cell state persistence, reset, and `setLayerLstmH/C` round-trip; convergence on a fixed regression target |
| `test_train.cpp` | `test_train` | `observe()` increments count; save/load round-trips policy weights and stats; empty kernel early-return; avgLoss/lastLoss tracking; replay buffer cap and sampling; sequence branch toggle; `reset()` clears stats without touching weights; multiple observations drive loss down |
| `test_training_phase.cpp` | `test_training_phase` | Training-phase FSM transitions; GUI scene logic for the animated training screen |
| `test_log.cpp` | `test_log` | `AppLogger` starts empty; `log()` appends entries, records id/type; ring buffer caps at `MAX_LOG_ENTRIES`; `addHistory()` preserves order; dedup suppresses identical messages; `flush()` is safe without file logging |

---

## Writing a New Test

1. Create `test/test_<module>.cpp` and include `<catch2/catch_test_macros.hpp>`.
2. Add a target block to `test/CMakeLists.txt`:
   ```cmake
   add_executable(test_<module> test_<module>.cpp)
   target_include_directories(test_<module> PRIVATE ${CMAKE_SOURCE_DIR}/src)
   target_link_libraries(test_<module> PRIVATE core Catch2::Catch2WithMain)
   add_test(NAME <module>_test COMMAND test_<module>)
   ```
3. Rebuild and run `ctest --test-dir build/linux-debug` to verify.

See `.github/prompts/test-app.prompt.md` for the full authoring guide.
