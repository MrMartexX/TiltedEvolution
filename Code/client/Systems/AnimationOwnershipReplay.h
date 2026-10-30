#pragma once

#include <cstdint>

// Retire work admitted under an earlier ownership epoch. This only clears
// queued playback; it does not reset the actor's current animation graph.
template <class TQueue>
bool RetireAnimationReplayForOwnershipChange(
    uint32_t aCurrentEpoch, uint32_t aIncomingEpoch, TQueue& aActions,
    uint32_t& aReplayCount, bool& aPendingGraphReset)
{
    if (aIncomingEpoch == 0 || (aCurrentEpoch != 0 && aIncomingEpoch <= aCurrentEpoch))
        return false;

    aActions.clear();
    aReplayCount = 0;
    aPendingGraphReset = false;
    return true;
}
