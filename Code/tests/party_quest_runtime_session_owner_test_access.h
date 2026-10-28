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

        // Existing low-level process-owner tests intentionally bypass the
        // production bootstrap. Keep that authority explicit, test-only and
        // one-shot so it cannot leak into production integration code.
        owner.m_allowNextDirectProcessBindForTesting = true;
    }

    static void AuthorizeNextDirectProcessBindForTesting() noexcept
    {
        PartyQuestRuntimeSessionOwner::GetProcessOwner()
            .m_allowNextDirectProcessBindForTesting = true;
    }

    static void RevokeDirectProcessBindForTesting() noexcept
    {
        PartyQuestRuntimeSessionOwner::GetProcessOwner()
            .m_allowNextDirectProcessBindForTesting = false;
    }

    [[nodiscard]] static PartyQuestRuntimeApplySession*
    GetMutableProcessSessionForTesting() noexcept
    {
        return PartyQuestRuntimeSessionOwner::GetProcessOwner().m_session.get();
    }
};