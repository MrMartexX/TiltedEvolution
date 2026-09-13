#include <Structs/Skyrim/QuestSnapshot.h>

#include <catch2/catch.hpp>

#include <algorithm>

namespace
{
QuestSnapshot BuildSnapshot()
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(2, 0x12345);
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 40;
    snapshot.Revision = 7;
    snapshot.InitiatorPlayerId = 3;
    snapshot.SceneParticipantPlayerId = 3;

    snapshot.CompletedStages = {40, 10, 20};
    snapshot.Objectives = {
        {20, QuestObjectiveState::Displayed},
        {10, QuestObjectiveState::Completed}
    };
    snapshot.ReferenceAliases = {
        {7, GameId(1, 0x200), false},
        {2, GameId(1, 0x100), true}
    };
    snapshot.LocationAliases = {
        {5, GameId(1, 0x500)},
        {1, std::nullopt}
    };
    snapshot.CreatedReferences = {
        GameId(3, 0x300),
        GameId(3, 0x200)
    };

    return snapshot;
}
} // namespace

TEST_CASE("QuestSnapshot digest ignores collection insertion order", "[quest.snapshot]")
{
    auto first = BuildSnapshot();
    auto second = BuildSnapshot();

    std::reverse(second.CompletedStages.begin(), second.CompletedStages.end());
    std::reverse(second.Objectives.begin(), second.Objectives.end());
    std::reverse(second.ReferenceAliases.begin(), second.ReferenceAliases.end());
    std::reverse(second.LocationAliases.begin(), second.LocationAliases.end());
    std::reverse(second.CreatedReferences.begin(), second.CreatedReferences.end());

    REQUIRE(first.ComputeDigest() == second.ComputeDigest());
}

TEST_CASE("QuestSnapshot digest detects hidden quest-state changes", "[quest.snapshot]")
{
    auto canonical = BuildSnapshot();
    auto divergent = BuildSnapshot();

    divergent.ReferenceAliases[0].ReferenceId = GameId(1, 0x201);

    REQUIRE(canonical.ComputeDigest() != divergent.ComputeDigest());
}

TEST_CASE("QuestSnapshot digest distinguishes failed quest state", "[quest.snapshot]")
{
    auto running = BuildSnapshot();
    auto failed = BuildSnapshot();
    failed.Status = QuestSnapshotStatus::Failed;

    REQUIRE(running.ComputeDigest() != failed.ComputeDigest());
    REQUIRE(QuestSnapshot::SchemaVersion == 2);
}

TEST_CASE("QuestSnapshot canonicalization removes exact duplicates", "[quest.snapshot]")
{
    auto snapshot = BuildSnapshot();
    snapshot.CompletedStages.push_back(20);
    snapshot.Objectives.push_back(snapshot.Objectives.front());
    snapshot.ReferenceAliases.push_back(snapshot.ReferenceAliases.front());
    snapshot.LocationAliases.push_back(snapshot.LocationAliases.front());
    snapshot.CreatedReferences.push_back(snapshot.CreatedReferences.front());

    snapshot.Canonicalize();

    REQUIRE(snapshot.CompletedStages.size() == 3);
    REQUIRE(snapshot.Objectives.size() == 2);
    REQUIRE(snapshot.ReferenceAliases.size() == 2);
    REQUIRE(snapshot.LocationAliases.size() == 2);
    REQUIRE(snapshot.CreatedReferences.size() == 2);
}

TEST_CASE("Quest sync classifier distinguishes gameplay and hidden service candidates", "[quest.snapshot.classification]")
{
    PartyQuestSyncFacts mainQuest;
    mainQuest.QuestType = 1;
    mainQuest.HasStages = true;
    const PartyQuestSyncClassification expectedMain{
        PartyQuestSyncClass::SharedCandidate,
        PartyQuestSyncReason::GameplayQuestType};
    REQUIRE(ClassifyPartyQuestSync(mainQuest) == expectedMain);

    PartyQuestSyncFacts hiddenUntyped;
    hiddenUntyped.QuestType = 0;
    hiddenUntyped.HasStages = true;
    const PartyQuestSyncClassification expectedHiddenUntyped{
        PartyQuestSyncClass::ServiceCandidate,
        PartyQuestSyncReason::HiddenUntyped};
    REQUIRE(ClassifyPartyQuestSync(hiddenUntyped) == expectedHiddenUntyped);

    PartyQuestSyncFacts hiddenMisc;
    hiddenMisc.QuestType = 6;
    hiddenMisc.HasStages = true;
    const PartyQuestSyncClassification expectedHiddenMisc{
        PartyQuestSyncClass::ServiceCandidate,
        PartyQuestSyncReason::HiddenMiscellaneous};
    REQUIRE(ClassifyPartyQuestSync(hiddenMisc) == expectedHiddenMisc);
}

TEST_CASE("Quest sync classifier keeps user-facing none and miscellaneous quests eligible", "[quest.snapshot.classification]")
{
    PartyQuestSyncFacts namedUntyped;
    namedUntyped.QuestType = 0;
    namedUntyped.HasStages = true;
    namedUntyped.HasDisplayName = true;
    const PartyQuestSyncClassification expectedNamedUntyped{
        PartyQuestSyncClass::SharedCandidate,
        PartyQuestSyncReason::UserFacingUntyped};
    REQUIRE(ClassifyPartyQuestSync(namedUntyped) == expectedNamedUntyped);

    PartyQuestSyncFacts hudMisc;
    hudMisc.QuestType = 6;
    hudMisc.HasStages = true;
    hudMisc.IsDisplayedInHud = true;
    const PartyQuestSyncClassification expectedHudMisc{
        PartyQuestSyncClass::SharedCandidate,
        PartyQuestSyncReason::UserFacingMiscellaneous};
    REQUIRE(ClassifyPartyQuestSync(hudMisc) == expectedHudMisc);

    PartyQuestSyncFacts noStages;
    noStages.QuestType = 1;
    noStages.HasStages = false;
    const PartyQuestSyncClassification expectedNoStages{
        PartyQuestSyncClass::LocalOnly,
        PartyQuestSyncReason::NoStages};
    REQUIRE(ClassifyPartyQuestSync(noStages) == expectedNoStages);
}
