#include <Structs/Skyrim/PartyQuestAdmission.h>

#include <algorithm>

namespace
{
const std::vector<GameId> kConfirmedServiceQuests{
    // WIGreeting (Skyrim.esm)
    GameId(0, 0x000C7919),
    // CRHoldExpansion (Skyrim.esm)
    GameId(0, 0x000F9075),
    // DLC1ScrollHandlingChangeLoc (Dawnguard.esm in the validated load order)
    GameId(2, 0x00012F92),
};
} // namespace

PartyQuestAdmissionDecision PartyQuestAdmissionPolicy::Evaluate(
    const GameId& acQuestId,
    const PartyQuestSyncFacts& acFacts) noexcept
{
    PartyQuestAdmissionDecision decision;
    decision.Classification = ClassifyPartyQuestSync(acFacts);

    if (IsConfirmedServiceQuest(acQuestId))
    {
        decision.Status = PartyQuestAdmissionStatus::BlockedConfirmedServiceQuest;
        return decision;
    }

    switch (decision.Classification.Class)
    {
    case PartyQuestSyncClass::SharedCandidate:
        decision.Status = PartyQuestAdmissionStatus::SharedProvisional;
        break;
    case PartyQuestSyncClass::ServiceCandidate:
        decision.Status = PartyQuestAdmissionStatus::BlockedServiceCandidate;
        break;
    case PartyQuestSyncClass::LocalOnly:
        decision.Status = PartyQuestAdmissionStatus::BlockedLocalOnly;
        break;
    }

    return decision;
}

bool PartyQuestAdmissionPolicy::IsConfirmedServiceQuest(const GameId& acQuestId) noexcept
{
    return std::find(kConfirmedServiceQuests.begin(), kConfirmedServiceQuests.end(), acQuestId) !=
        kConfirmedServiceQuests.end();
}

const std::vector<GameId>& PartyQuestAdmissionPolicy::GetConfirmedServiceQuestIds() noexcept
{
    return kConfirmedServiceQuests;
}
