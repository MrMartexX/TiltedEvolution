#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeRecovery.h>

class PartyQuestRuntimeRecoveryCoordinatorTestAccess final
{
public:
    [[nodiscard]] static PartyQuestRuntimeRecoveryResult ResolveCrashRecovery(
        PartyQuestRuntimeApplySession& aSession,
        const PartyQuestCoopSavePaths& acPaths) noexcept
    {
        return PartyQuestRuntimeRecoveryCoordinator::ResolveCrashRecovery(
            aSession,
            acPaths);
    }

    [[nodiscard]] static PartyQuestRuntimeRecoveryResult ResolveLiveRecovery(
        PartyQuestRuntimeApplySession& aSession,
        const PartyQuestCoopSavePaths& acPaths) noexcept
    {
        return PartyQuestRuntimeRecoveryCoordinator::ResolveLiveRecovery(
            aSession,
            acPaths);
    }
};
