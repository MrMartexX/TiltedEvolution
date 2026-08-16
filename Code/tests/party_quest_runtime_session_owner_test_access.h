#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>

class PartyQuestRuntimeSessionOwnerTestAccess final
{
public:
    static void ForceClearProcessOwner() noexcept
    {
        auto& owner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
        auto& guard = PartyQuestSaveGuard::GetProcessGuard();
        if (guard.IsActive())
            (void)guard.Release(guard.GetTransactionId());
        owner.Clear();
    }
};
