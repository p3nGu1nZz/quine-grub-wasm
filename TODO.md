# TODO

Tracks remaining work. Completed items are kept for reference.

## Completed

- [x] Rename `window_panels.cpp` to `panels.cpp` and add header file
- [x] Shrink network architecture and add replay buffer
- [x] Add FPS counter to GUI and fix build errors after refactor
- [x] Implement telemetry wizardry, advisor scoring, and blacklist logic
- [x] Add extensive unit tests for adviser, trainer, replay, GUI, CLI
- [x] Update docs/specs for neural network, telemetry, heuristics
- [x] Add automatic evolution↔training trigger and GUI changes
- [x] Implement `Trainer::reset()` and call during auto-training
- [x] Add WAT sequence kernel (`sequence_model.wat`) and `KERNEL_SEQ`
- [x] Base64-encode sequence kernel and embed in constants.h
- [x] New test for kernel weight callback and cyclical loop behaviour
- [x] Update architecture and telemetry documentation
- [x] Post summary comment on issue and close first batch of TODOs
- [x] Implement in-kernel sequence predictor (KERNEL_SEQ) that runs inside WASM at mutation time
- [x] Provide CLI flag `--kernel=glob|seq` to select kernel type
- [x] Add CLI options `--save-model` / `--load-model` / `--kernel`
- [x] Rewrite README.md with humanised prose, architecture diagram, clean tables
- [x] Rename project repo from WASM-Quine-Bootload to quine-grub-wasm throughout docs
- [x] Fix all ML/RL math: Xavier init, chain-rule backpropagation through all dense layers
- [x] Implement LSTM single-step BPTT (gate gradients + weight update + delta propagation)
- [x] Integrate `Loss::compute()` quality penalty into training target
- [x] Persist LSTM hidden/cell state in checkpoint save/load
- [x] Fix `record_weight` signature fallback (v(ii) → v(ff)) for KERNEL_SEQ
- [x] Fix `runDynamic` for memory-less kernels (KERNEL_SEQ)
- [x] Full feature extraction (opcode histogram, bigrams, structural metadata, positional sequence)
- [x] GUI heatmap polish, layout fix (no scrollbars), training animations
- [x] SDL3 per-file compile progress in setup.sh
- [x] Unit test coverage: 16 test suites covering all major modules

## Pending / Future Work

- [ ] Allow evolution and training to proceed concurrently (producer-consumer telemetry queue)
- [ ] Package a standalone ISO distribution
- [ ] Extend auto-train logic to adaptively schedule based on loss plateau and buffer staleness
- [ ] Investigate exporting the trained model to the kernel and enabling in-WASM weight updates
- [ ] Experience inheritance — propagate weight snapshot from parent to child kernel via `env.spawn`
- [ ] Performance optimisations for the GUI heatmap and training timeline

### New experimental tasks (self-evolving kernels)

- [ ] Add persistent kernel state store (per-kernel state blob passed across generations; stored in bin/seq/<runid>/state_<n>.dat)
- [ ] Add candidate evaluation harness (small supervised/interactive tasks + scalar fitness measurement)
- [ ] Create curriculum of tasks (toy boolean → sequence prediction → tiny image patches)
- [ ] Implement experience inheritance and state merge policies in evolveBinary
- [ ] Experiment: optional host primitives (dot-product, conv helper) behind a feature flag
- [ ] Experiment harness & telemetry: reproducible seeds, metrics (opcode histograms, float-op counts, memory hotspots)

