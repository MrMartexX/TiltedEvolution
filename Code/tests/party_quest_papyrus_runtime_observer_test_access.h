#pragma once

#include <Structs/Skyrim/PartyQuestPapyrusRuntimeMonitor.h>

/**
 * Test-only issuer for observer trust. Production code cannot obtain a live
 * observer authorization from the public observer interface alone.
 */
class PartyQuestPapyrusRuntimeObserverTestAccess final
{
public:
    [[nodiscard]] static PartyQuestPapyrusRuntimeObserverAuthorization Authorize(
        const PartyQuestPapyrusRuntimeObserver& acObserver) noexcept
    {
        return PartyQuestPapyrusRuntimeObserverAuthorization(acObserver);
    }
};
