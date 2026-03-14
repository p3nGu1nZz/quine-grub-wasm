# Mutation Heuristic – Specification

## Purpose

Describe the adaptive algorithm that monitors kernel execution failures
and biases the evolution engine to avoid repeating destructive edits.
The heuristic helps the bootloader recover more quickly from traps and
improve long-term survival rate of mutated kernels.

## Inputs

- History of generations, including which mutations were applied and
  whether execution resulted in a trap or successful quine verification.
- Optional external parameters provided via CLI flags (e.g. `--mutation-strategy` or `--heuristic=<none|blacklist|decay>`).

## Behaviour

1. Maintain a small "blacklist" of mutation patterns that previously led
   to unreachable or other traps (e.g. inserting an `unreachable` opcode
   or appending a balanced `[i32.const,x, drop]` sequence where `x` is
   out of range).
2. After each generation:
   - If execution succeeded, optionally decay the blacklist entries
     (allowing them to be retried later).  This behaviour is enabled when
     the CLI `--heuristic` flag is set to `decay` rather than plain
     `blacklist`.
   - If execution trapped, record the mutation that preceded the failure
     and add it to the blacklist (unless the heuristic is disabled).
3. When selecting a random mutation for the next generation, consult the
   blacklist and reroll if the candidate matches a forbidden pattern.  The
   reroll logic is only active when any heuristic mode besides `none` is
   chosen; the `--mutation-strategy` flag itself is orthogonal and merely
   selects between `random`, `blacklist`, or `smart` sampling of the
   known-instruction pool.
4. Expose CLI flag `--heuristic=<none|blacklist|decay>` to control
   whether the heuristic is active and whether its entries decay over time.
5. The heuristic state is now persisted between runs.  On
   shutdown the blacklist is saved to `blacklist.txt` in the telemetry
   directory and automatically reloaded on startup, so the bootloader can
   remember prior trap‑inducing sequences across process restarts.
   Export reports may still include blacklist contents for offline
   analysis.

## Outputs / Side Effects

- The evolving kernel may avoid repeating the same trap-inducing edits,
  reducing the frequency of crashes and repairs.
- Telemetry exports should optionally include a summary of the blacklist
  (e.g. count of entries) when the heuristic is enabled.

## Constraints

- The heuristic logic must not introduce significant overhead; it should
  be simple enough to run in O(1) or O(k) where k is the blacklist size.
- Blacklist comparisons should be structural rather than full binary
  equality to handle mutations that differ only in operand values.

## Open Questions

- Should the blacklist be shared across parallel runs (e.g. via a file)?
- Could a positive reinforcement mechanism (reward safe mutations) be
  combined with the blacklist?
