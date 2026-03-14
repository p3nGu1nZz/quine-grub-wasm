#include "fsm.h"

#include <SDL3/SDL.h>

bool BootFsm::transition(SystemState to) {
    SystemState from = m_current;
    if (from == to) return false; // no-op: already in this state
    m_current   = to;
    m_enteredAt = static_cast<uint64_t>(SDL_GetTicks());
    if (m_onTransition)
        m_onTransition(from, to);
    return true;
}

uint64_t BootFsm::elapsedMs() const {
    return static_cast<uint64_t>(SDL_GetTicks()) - m_enteredAt;
}
