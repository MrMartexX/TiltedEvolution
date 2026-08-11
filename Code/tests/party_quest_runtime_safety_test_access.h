#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeSafety.h>

/**
 * Test-only capability factory for low-level runtime lifecycle tests that are
 * intentionally independent from compatibility-manifest setup.
 *
 * No production implementation or public constructor exists for
 * PartyQuestRuntimeMutationAuthorization.
 */
class PartyQuestRuntimeSafetyTestAccess final
{
public:
    [[nodiscard]] static PartyQuestRuntimeMutationAuthorization MakeMutationAuthorization(
        const QuestSnapshot& acSnapshot,
        PartyQuestApplyAction aActions,
        bool aDryRunOnly = true,
        PartyQuestVerificationComponent aAdapterMutationComponents =
            PartyQuestVerificationComponent::QuestSnapshot) noexcept
    {
        constexpr uint64_t kTestCompatibilityFingerprint = 0xD15EA5E5AFE00001ull;
        return PartyQuestRuntimeMutationAuthorization(
            acSnapshot.QuestId,
            acSnapshot.ComputeDigest(),
            kTestCompatibilityFingerprint,
            aAdapterMutationComponents,
            aActions,
            aDryRunOnly);
    }

    /**
     * Low-level lifecycle tests model a future executable adapter explicitly.
     * Production BuildApplyPlan() remains DryRunOnly=true and cannot call this.
     */
    static void AuthorizePlan(
        PartyQuestApplyPlan& aPlan,
        const QuestSnapshot& acSnapshot) noexcept
    {
        aPlan.DryRunOnly = false;
        aPlan.MutationAuthorization = MakeMutationAuthorization(
            acSnapshot,
            aPlan.Actions,
            false);
    }

    static void AuthorizeDryRunPlan(
        PartyQuestApplyPlan& aPlan,
        const QuestSnapshot& acSnapshot) noexcept
    {
        aPlan.DryRunOnly = true;
        aPlan.MutationAuthorization = MakeMutationAuthorization(
            acSnapshot,
            aPlan.Actions,
            true);
    }
};
