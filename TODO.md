# Issue #101 TODO

This document tracks the remaining work for issue #101 and related
infrastructure.  Completed items are kept for reference; pending tasks are
marked with `[ ]`.

## Completed

- [x] Rename `window_panels.cpp` to `panels.cpp` and add header file
- [x] Shrink network architecture and add replay buffer
- [x] Add FPS counter to GUI and fix build errors after refactor
- [x] Implement telemetry wizardry, advisor scoring, and blacklist logic
- [x] Add extensive unit tests for adviser, trainer, replay, GUI, CLI
- [x] Update docs/specs for neural network, telemetry, heuristics
- [x] Add automatic evolution↔training trigger and ARM20 UI changes
- [x] Implement `Trainer::reset()` and call during auto-training
- [x] Add WAT sequence kernel (`sequence_model.wat`) and `KERNEL_SEQ`
- [x] Base64-encode sequence kernel and embed in constants.h
- [x] New test for kernel weight callback and cyclical loop behaviour
- [x] Update architecture and telemetry documentation
- [x] Add comments for secondary progress bar in GUI
- [x] Refresh issue description with autoregressive model vision
- [x] Post summary comment on issue and close first batch of TODOs

## Pending / future work

- [x] Implement in-kernel sequence predictor that runs inside WASM at
      mutation time (seed with self-replicating quine variations).  The
      kernel is executed by `evolveBinary()` and any weights it reports are
      exposed via `EvolutionResult::weightFeedback`.
- [x] Provide CLI flag/option to select between histogram network and
      sequence-model kernel (`--kernel=glob|seq`).
- [ ] Allow evolution and training to proceed concurrently (producer-
      consumer telemetry queue).
- [ ] Package a standalone ISO distribution of the bootloader.
- [x] Add CLI options for checkpoint paths, model hot-reload, and kernel
      selection.  (`--save-model` / `--load-model` / `--kernel` already
      exist.)
- [ ] Extend auto-train logic to adaptively schedule based on loss plateau
      and buffer staleness.
- [ ] Investigate exporting the trained model to the kernel and enabling
      in-WASM weight updates.
- [ ] Performance optimisations for the GUI heatmap and training timeline.

(The list may be updated as new ideas surface.)
