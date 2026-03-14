# Multi-Instance Semantics

The bootloader may manage multiple concurrent WASM kernels, each running
its own copy of the program.  This document specifies the behaviour of
instances and the host interfaces for spawning and controlling them.

## Instance Lifecycle

- **Spawn**: A running kernel may call the imported function
  `env.spawn(ptr:length)` to request that the bootloader create a new
  instance.  The bytes pointed to by `ptr`/`length` are interpreted as a
  base64-encoded kernel image (typically the caller's own quine or a
  mutated variant).
- **Kill**: Later generations may choose to terminate a previously
  spawned sibling by calling `env.kill_instance(idx)` where `idx`
  corresponds to the zero-based index shown in the GUI overview panel.
  This function is currently a no-op if the index is invalid.
- **Storage**: Spawned kernels are kept in a vector inside `App` and may
  be executed in future cycles; initially they are inert until an
  explicit scheduler is implemented.
- **Visibility**: The GUI status bar displays `Instances: N` where `N`
  is the number of spawned kernels.  A dedicated **Instances** panel
  (activated when one or more kernels exist) shows each base64 blob and
  offers a **Kill** button next to each entry.  This panel also includes
  basic telemetry counters and will be expanded in future releases.
- **Termination**: There is currently no mechanism to kill individual
  instances; all instances are lost when the parent process exits.

## Host API (Imported Functions)

- `env.spawn(ptr:u32,len:u32)` – create a new instance containing the
  given base64 kernel.  Always linked, even if the kernel chooses not to
  call it.
- `env.kill_instance(idx:i32)` – request that the host remove the
  corresponding instance.  The call returns void and kernels should not
  rely on synchronous destruction; the request is logged for tracing.
- In future, `env.kill_instance(id:i32)` may be added to allow programmatic
  termination.

## Bootloader Behaviour

- Spawns are logged via `App::spawnInstance` and appear in telemetry
  exports as an entry in the history log.
- The initial implementation does not execute spawned instances; they
  are available for offline inspection by tools or GUI panels.

## Data Persistence

- Telemetry exports now include an additional section `INSTANCES:`
  listing any base64 blobs received via `env.spawn` during the run.  The
  section reflects the *currently alive* instances; killed kernels are
  removed when the export is generated.  Parsers should skip this
  section if it is absent.

## Notes

This is a lightweight framework to support experimentation with
self-replicating kernels.  The multi-instance model enables ideas such
as competition between sibling programs, ensemble voting, or genetic
crossover.
