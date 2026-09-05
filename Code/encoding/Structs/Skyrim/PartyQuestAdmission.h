#pragma once

#include <Structs/GameId.h>
#include <Structs/Skyrim/QuestSnapshot.h>

#include <cstdint>
#include <vector>

/**
 * Server-side admission result for one observed Skyrim quest.
 *
 * SharedProvisional is intentionally not equivalent to permission to mutate
 * Skyrim runtime state. It only permits the quest into the diagnostic
 * canonical campaign while stronger compatibility/runtime-safety checks are
 * still being developed.
 */
enum class PartyQuestAdmissionStatus : uint8_t
{
    SharedProvisional,
    BlockedServiceCandidate,
    BlockedLocalOnly,
    BlockedConfirmedServiceQuest
};

struct PartyQuestAdmissionDecision
{
    PartyQuestAdmissionStatus Status{PartyQuestAdmissionStatus::BlockedLocalOnly};
    PartyQuestSyncClassification Classification;

    [[nodiscard]] bool IsAdmitted() const noexcept
    {
        return Status == PartyQuestAdmissionStatus::SharedProvisional;
    }
};

/**
 * Conservative admission policy shared by the server and repair planner.
 *
 * Runtime facts are supplied by the observing client and classified again on
 * the server. Confirmed service quest identities override those facts so known
 * controller/tracker quests cannot be admitted by claiming user-facing data.
 */
class PartyQuestAdmissionPolicy final
{
public:
    [[nodiscard]] static PartyQuestAdmissionDecision Evaluate(
        const GameId& acQuestId,
        const PartyQuestSyncFacts& acFacts) noexcept;

    [[nodiscard]] static bool IsConfirmedServiceQuest(const GameId& acQuestId) noexcept;
    [[nodiscard]] static const std::vector<GameId>& GetConfirmedServiceQuestIds() noexcept;
};
