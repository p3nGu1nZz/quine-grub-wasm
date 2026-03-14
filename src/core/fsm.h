#pragma once

#include "types.h"
#include <functional>

// ── BootFsm ───────────────────────────────────────────────────────────────────
//
// Finite State Machine that governs the WASM Quine Bootloader lifecycle.
//
// Allowed transitions:
//   IDLE  ──▶  BOOTING  ──▶  LOADING_KERNEL  ──▶  EXECUTING
//                                                      │         │
//                                               VERIFYING_QUINE  │
//                                                      │         ▼
//                                                   (reboot)  REPAIRING
//                                                      │         │
//                                                      └────┬────┘
//                                                           ▼
//                                                         IDLE
// ─────────────────────────────────────────────────────────────────────────────

class BootFsm {
public:
    using OnTransition = std::function<void(SystemState from, SystemState to)>;

    // Register a callback invoked on every state transition.
    // Called synchronously inside transition().
    void setTransitionCallback(OnTransition cb) { m_onTransition = std::move(cb); }

    // Perform a guarded state transition. Does nothing if from == to.
    // Returns true when the transition was accepted, false for no-ops.
    bool transition(SystemState to);

    // Query the current state.
    SystemState current() const { return m_current; }

    // How long (in ms) since the last transition? (uses SDL_GetTicks)
    uint64_t elapsedMs() const;

    // Timestamp (SDL_GetTicks) of the last transition.
    uint64_t enteredAt() const { return m_enteredAt; }

private:
    SystemState  m_current   = SystemState::IDLE;
    uint64_t     m_enteredAt = 0;
    OnTransition m_onTransition;
};
