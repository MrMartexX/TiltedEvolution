#pragma once

// A replay may request only a graph reset, with no actions left to play.
// Keep that request pending until the graph is ready, independently of the queue.
template <class TIsReady, class TReset>
bool TryApplyAnimationReplayReset(bool& aPendingReset, TIsReady aIsReady, TReset aReset)
{
    if (!aPendingReset)
        return true;
    if (!aIsReady())
        return false;

    if (!aReset())
        return false;
    aPendingReset = false;
    return true;
}
