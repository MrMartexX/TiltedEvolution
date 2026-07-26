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
