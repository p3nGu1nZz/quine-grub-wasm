# Unit Tests

This directory contains unit tests for the WASM Quine Bootloader C++ project.  Tests are written using **Catch2** (v3) and the `setup.sh` helper will download+build Catch2 into `external/Catch2` when preparing the workspace.

## Layout

Tests live in the top‑level `test/` directory. Each file typically exercises a single module or feature.

| File | Description |
|------|-------------|
| `test_state.cpp` | smoke check for `stateStr` (example Catch2 test)
| `test_util_dpi.cpp` | verifies DPI helpers, that `Gui` applies both the raw DPI and boosted UI scales during init, and checks automatic export file generation

Additional tests (e.g. `test_wasm_kernel.cpp`, `test_util.cpp`, `test_evolution.cpp`) can be added as needed; see the companion test-app skill and guidelines for writing new tests.

## Running Tests

```bash
bash scripts/test.sh
```

This builds the project (if needed), builds all test targets, and runs them via `ctest`.

## Writing a New Test

See `.github/prompts/test-app.prompt.md` for the full guide, including how to register
a new test in `CMakeLists.txt`.
