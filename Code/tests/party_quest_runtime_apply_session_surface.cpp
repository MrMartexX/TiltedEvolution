#ifdef TP_PARTY_QUEST_LOW_LEVEL_TEST_ACCESS
#undef TP_PARTY_QUEST_LOW_LEVEL_TEST_ACCESS
#endif

#include <Structs/Skyrim/PartyQuestRuntimeApplySession.h>

#include <cstdint>

template <class T>
concept HasDirectCheckpointPublication = requires(T& aSession, uint64_t aTransactionId)
{
    aSession.MarkCheckpointCreated(aTransactionId);
};

template <class T>
concept HasDirectMutationArm = requires(T& aSession, uint64_t aTransactionId)
{
    aSession.ArmRuntimeMutation(aTransactionId);
};

template <class T>
concept HasBeginSurface = requires(T& aSession, const PartyQuestRuntimeApplyRequest& acRequest)
{
    aSession.Begin(acRequest);
};

static_assert(HasBeginSurface<PartyQuestRuntimeApplySession>);
static_assert(!HasDirectCheckpointPublication<PartyQuestRuntimeApplySession>);
static_assert(!HasDirectMutationArm<PartyQuestRuntimeApplySession>);
