# Quine & Self-Reboot Semantics

## Motivation

The long-term vision for the evolving system is to push as much control
logic as possible into the WASM program itself, relegating the bootloader
to a minimal interpreter that simply runs the provided kernel blob.  In
this model the kernel contains the neural network, mutation strategy and
any heuristics; it drives its own evolution and decides when it is ready
for the next generation.

Currently the bootloader drives the generation loop: it loads a kernel,
executes it, verifies that the program emitted a correct quine string via
the logging host call, mutates the kernel, and then restarts.

## Goals

1. **Intra-kernel reboot request** – provide a host import (e.g.
   `env.reboot`) that the WASM program can call with a pointer/length to
   a new base64-encoded kernel.  The bootloader will then abort the
   current execution and immediately begin the boot sequence using the
   supplied bytes.  This allows the kernel to encode its own mutation
   logic and to choose when to fork.

2. **Quine return value** – rather than relying on stdout for the quine
   string, the kernel should ideally return the new program via an
   exported function result or write it to a well-known linear memory
   region.  The host can read this directly without requiring the
   `env.log` trampoline.  This simplifies the interface and makes the
   quine contract explicit.

3. **Error handling** – the kernel should be able to signal failure by
   returning a non-zero status or by omitting the quine.  The bootloader
   treats these as a generation failure and may apply a heuristic
   blacklist accordingly.

## Design Notes

- A reboot might look like:
  ```wat
  (import "env" "reboot" (func $reboot (param i32 i32)))
  (export "run" (func $run))
  (func $run
     ;; compute new kernel into memory at offset 0x100, length in $len
     call $reboot (i32.const 0x100) (local.get $len)
  )
  ```
  The bootloader's `reboot` host function would read the specified
  memory, convert it to a string, and queue it as `m_nextKernel` before
  terminating the current execution.

- The quine contract may be simplified to `env.reboot` itself; the
  kernel can simply call `reboot` with its own base64 representation,
  eliminating the need for separate log parsing.  This makes the
  contract explicit and easier for future host implementations (e.g. a
  remote server or competition harness).

- A companion spec should eventually document the in-memory format of
  the neural weight matrix and how compressed experience is shared
  across generations (see issue #13).

## Next Steps

- Add unit tests exercising the new host import and verifying that the
  bootloader correctly switches kernels mid-run.
- Modify the kernel generator tools to emit the appropriate `reboot`
  call when constructing simulated kernels for tests.
- Update the `App::onWasmLog` handler to optionally read the quine from an
  explicit memory region when `reboot` is not used.

This spec will evolve once the implementation details are settled and
additional features (e.g. multi-instance spawning) are added.