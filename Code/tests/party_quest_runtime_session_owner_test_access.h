#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>
#include <Structs/Skyrim/PartyQuestRuntimeOwner.h>

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

        // The process aggregate is intentionally one-way at production shutdown.
        // Tests reuse the process singleton, so reset its admission state together
        // with the owned session instead of leaking a prior test boundary.
        auto& aggregate = PartyQuestRuntimeOwner::GetProcessOwner();
        {
            std::lock_guard lock(aggregate.m_mutex);
            aggregate.m_deferredWorld.Clear();
            aggregate.m_ownerEpoch = 1;
            aggregate.m_boundGeneration = 0;
            aggregate.m_boundCampaignId.reset();
            aggregate.m_boundPlayerProfileId.reset();
            aggregate.m_connected = false;
            aggregate.m_inParty = false;
            aggregate.m_runtimeSessionBound = false;
            aggregate.m_lifecycleInvalidationPending = false;
            aggregate.m_shutdown = false;
        }

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
