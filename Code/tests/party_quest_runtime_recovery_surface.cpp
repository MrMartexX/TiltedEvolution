#include <Structs/Skyrim/PartyQuestRuntimeRecovery.h>

#include <concepts>

template <class T>
concept HasDirectCrashRecovery = requires(
    PartyQuestRuntimeApplySession& aSession,
    const PartyQuestCoopSavePaths& acPaths)
{
    T::ResolveCrashRecovery(aSession, acPaths);
};

template <class T>
concept HasDirectLiveRecovery = requires(
    PartyQuestRuntimeApplySession& aSession,
    const PartyQuestCoopSavePaths& acPaths)
{
    T::ResolveLiveRecovery(aSession, acPaths);
};

static_assert(!HasDirectCrashRecovery<PartyQuestRuntimeRecoveryCoordinator>);
static_assert(!HasDirectLiveRecovery<PartyQuestRuntimeRecoveryCoordinator>);
