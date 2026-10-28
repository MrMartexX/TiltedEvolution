#pragma once

#include <TiltedCore/Stl.hpp>
#include <Structs/ActionEvent.h>

// Relay and replay share precisely the same accepted set.
template <class TPredicate, class TCache>
TiltedPhoques::Vector<ActionEvent> AcceptAndCacheActionEvents(
    const TiltedPhoques::Vector<ActionEvent>& acActions, TPredicate aAccept, TCache& aCache)
{
    TiltedPhoques::Vector<ActionEvent> accepted;
    for (const auto& action : acActions)
        if (aAccept(action))
            accepted.push_back(action);
    aCache.AppendAll(accepted);
    return accepted;
}
