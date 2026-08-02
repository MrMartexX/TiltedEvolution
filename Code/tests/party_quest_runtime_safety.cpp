#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Structs/Skyrim/PartyQuestRuntimeSafety.h>

#include <catch2/catch.hpp>

namespace
{
PartyQuestAdmissionDecision BuildAdmittedDecision(GameId aQuestId = GameId(7, 0x1000))
{
    PartyQuestSyncFacts facts;
    facts.QuestType = 1;
    facts.HasStages = true;
    facts.IsDisplayedInHud = true;
    facts.HasDisplayName = true;
    const auto decision = PartyQuestAdmissionPolicy::Evaluate(aQuestId, facts);
    REQUIRE(decision.Status == PartyQuestAdmissionStatus::SharedProvisional);
    return decision;
}

QuestSnapshot BuildSafetySnapshot(GameId aQuestId = GameId(7, 0x1000))
{
    QuestSnapshot snapshot;
    snapshot.QuestId = aQuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 20;
    snapshot.CompletedStages = {10, 20};
    snapshot.Objectives = {{20, QuestObjectiveState::Displayed}};
    return snapshot;
}
} // namespace

TEST_CASE("Runtime safety keeps admission separate from mutation authority", "[quest.party-state.runtime-safety]")
{
    PartyQuestSyncFacts serviceFacts;
    serviceFacts.QuestType = 0;
    serviceFacts.HasStages = true;
    serviceFacts.IsDisplayedInHud = false;
    serviceFacts.HasDisplayName = false;

    const GameId questId(7, 0x1100);
    const auto admission = PartyQuestAdmissionPolicy::Evaluate(questId, serviceFacts);
    REQUIRE_FALSE(admission.IsAdmitted());

    const auto plan = PartyQuestRuntimeSafetyPolicy::BuildApplyPlan(admission, BuildSafetySnapshot(questId));
    REQUIRE(plan.Safety.Status == PartyQuestRuntimeSafetyStatus::Blocked);
    REQUIRE(plan.Safety.Reason == PartyQuestRuntimeSafetyReason::AdmissionBlocked);
    REQUIRE(plan.Actions == PartyQuestApplyAction::None);
    REQUIRE(plan.DryRunOnly);
}

TEST_CASE("Simple admitted quest produces stage-only dry-run plan", "[quest.party-state.runtime-safety]")
{
    const auto admission = BuildAdmittedDecision();
    const QuestSnapshot snapshot = BuildSafetySnapshot();

    const auto plan = PartyQuestRuntimeSafetyPolicy::BuildApplyPlan(admission, snapshot);
    REQUIRE(plan.Safety.Status == PartyQuestRuntimeSafetyStatus::StageOnly);
    REQUIRE(plan.Safety.Reason == PartyQuestRuntimeSafetyReason::SimpleStageTransition);
    REQUIRE(plan.DryRunOnly);
    REQUIRE(plan.WouldMutateQuestStage());
    REQUIRE(HasPartyQuestApplyAction(plan.Actions, PartyQuestApplyAction::VerifyObjectives));
    REQUIRE(HasPartyQuestApplyAction(plan.Actions, PartyQuestApplyAction::WaitForPapyrusQuiescence));
    REQUIRE(HasPartyQuestApplyAction(plan.Actions, PartyQuestApplyAction::ResnapshotAndVerify));
    REQUIRE_FALSE(plan.Safety.IsRuntimeSafe());
}

TEST_CASE("Resolved world aliases defer generic stage repair", "[quest.party-state.runtime-safety]")
{
    const auto admission = BuildAdmittedDecision();
    QuestSnapshot snapshot = BuildSafetySnapshot();
    snapshot.ReferenceAliases = {
        {1, GameId(0, 0x1234), false},
        {2, GameId(0, 0x5678), false}
    };

    const auto plan = PartyQuestRuntimeSafetyPolicy::BuildApplyPlan(admission, snapshot);
    REQUIRE(plan.Safety.Status == PartyQuestRuntimeSafetyStatus::Deferred);
    REQUIRE(plan.Safety.Reason == PartyQuestRuntimeSafetyReason::ReferenceAliasesNeedWorld);
    REQUIRE(plan.Safety.Facts.ReferenceAliasCount == 2);
    REQUIRE(plan.Safety.Facts.UnresolvedReferenceAliasCount == 0);
    REQUIRE(HasPartyQuestApplyAction(plan.Actions, PartyQuestApplyAction::WaitForWorldTargets));
    REQUIRE(HasPartyQuestApplyAction(plan.Actions, PartyQuestApplyAction::WaitForPapyrusQuiescence));
    REQUIRE(plan.DryRunOnly);
}

TEST_CASE("Active scene participation defers even without aliases", "[quest.party-state.runtime-safety]")
{
    const auto admission = BuildAdmittedDecision();
    QuestSnapshot snapshot = BuildSafetySnapshot();
    snapshot.SceneParticipantPlayerId = 42;

    const auto decision = PartyQuestRuntimeSafetyPolicy::Evaluate(admission, snapshot);
    REQUIRE(decision.Status == PartyQuestRuntimeSafetyStatus::Deferred);
    REQUIRE(decision.Reason == PartyQuestRuntimeSafetyReason::SceneParticipantActive);
    REQUIRE(decision.Facts.HasSceneParticipant);
}

TEST_CASE("Unresolved and quest-object aliases require an adapter", "[quest.party-state.runtime-safety]")
{
    const auto admission = BuildAdmittedDecision();

    SECTION("unresolved reference")
    {
        QuestSnapshot snapshot = BuildSafetySnapshot();
        snapshot.ReferenceAliases = {{1, std::nullopt, false}};
        const auto decision = PartyQuestRuntimeSafetyPolicy::Evaluate(admission, snapshot);
        REQUIRE(decision.Status == PartyQuestRuntimeSafetyStatus::RequiresAdapter);
        REQUIRE(decision.Reason == PartyQuestRuntimeSafetyReason::UnresolvedReferenceAliases);
    }

    SECTION("quest object")
    {
        QuestSnapshot snapshot = BuildSafetySnapshot();
        snapshot.ReferenceAliases = {{1, GameId(0, 0x1234), true}};
        const auto decision = PartyQuestRuntimeSafetyPolicy::Evaluate(admission, snapshot);
        REQUIRE(decision.Status == PartyQuestRuntimeSafetyStatus::RequiresAdapter);
        REQUIRE(decision.Reason == PartyQuestRuntimeSafetyReason::QuestObjectAliases);
    }
}

TEST_CASE("Created references and location aliases require an adapter", "[quest.party-state.runtime-safety]")
{
    const auto admission = BuildAdmittedDecision();

    SECTION("created reference")
    {
        QuestSnapshot snapshot = BuildSafetySnapshot();
        snapshot.CreatedReferences = {GameId(8, 0x2000)};
        const auto decision = PartyQuestRuntimeSafetyPolicy::Evaluate(admission, snapshot);
        REQUIRE(decision.Status == PartyQuestRuntimeSafetyStatus::RequiresAdapter);
        REQUIRE(decision.Reason == PartyQuestRuntimeSafetyReason::CreatedReferences);
    }

    SECTION("location alias")
    {
        QuestSnapshot snapshot = BuildSafetySnapshot();
        snapshot.LocationAliases = {{1, std::nullopt}};
        const auto decision = PartyQuestRuntimeSafetyPolicy::Evaluate(admission, snapshot);
        REQUIRE(decision.Status == PartyQuestRuntimeSafetyStatus::RequiresAdapter);
        REQUIRE(decision.Reason == PartyQuestRuntimeSafetyReason::LocationAliases);
    }
}

TEST_CASE("Complex controller-like alias topology is not treated as generic stage-safe", "[quest.party-state.runtime-safety]")
{
    const auto admission = BuildAdmittedDecision();
    QuestSnapshot snapshot = BuildSafetySnapshot();

    for (size_t i = 0; i < PartyQuestRuntimeSafetyPolicy::kComplexAliasThreshold; ++i)
    {
        snapshot.ReferenceAliases.push_back({
            static_cast<uint32_t>(i),
            GameId(0, static_cast<uint32_t>(0x3000 + i)),
            false});
    }

    const auto decision = PartyQuestRuntimeSafetyPolicy::Evaluate(admission, snapshot);
    REQUIRE(decision.Status == PartyQuestRuntimeSafetyStatus::RequiresAdapter);
    REQUIRE(decision.Reason == PartyQuestRuntimeSafetyReason::ComplexAliasTopology);
    REQUIRE(decision.Facts.ReferenceAliasCount == PartyQuestRuntimeSafetyPolicy::kComplexAliasThreshold);
}

TEST_CASE("Inactive and terminal quest state are never generically replayed", "[quest.party-state.runtime-safety]")
{
    const auto admission = BuildAdmittedDecision();

    SECTION("inactive target")
    {
        QuestSnapshot snapshot = BuildSafetySnapshot();
        snapshot.Status = QuestSnapshotStatus::Inactive;
        const auto decision = PartyQuestRuntimeSafetyPolicy::Evaluate(admission, snapshot);
        REQUIRE(decision.Status == PartyQuestRuntimeSafetyStatus::RequiresAdapter);
        REQUIRE(decision.Reason == PartyQuestRuntimeSafetyReason::InactiveQuestState);
        REQUIRE(decision.Facts.IsInactiveState);
    }

    SECTION("terminal targets")
    {
        for (QuestSnapshotStatus status : {
                 QuestSnapshotStatus::Stopped,
                 QuestSnapshotStatus::Completed,
                 QuestSnapshotStatus::Failed})
        {
            QuestSnapshot snapshot = BuildSafetySnapshot();
            snapshot.Status = status;
            const auto decision = PartyQuestRuntimeSafetyPolicy::Evaluate(admission, snapshot);
            REQUIRE(decision.Status == PartyQuestRuntimeSafetyStatus::RequiresAdapter);
            REQUIRE(decision.Reason == PartyQuestRuntimeSafetyReason::TerminalQuestState);
            REQUIRE(decision.Facts.IsTerminalState);
        }
    }
}

TEST_CASE("Only an explicit verified native adapter can classify a quest RuntimeSafe", "[quest.party-state.runtime-safety]")
{
    const auto admission = BuildAdmittedDecision();
    QuestSnapshot risky = BuildSafetySnapshot();
    risky.Status = QuestSnapshotStatus::Completed;
    risky.ReferenceAliases = {{1, std::nullopt, true}};
    risky.LocationAliases = {{2, std::nullopt}};
    risky.CreatedReferences = {GameId(8, 0x4000)};

    const auto defaultDecision = PartyQuestRuntimeSafetyPolicy::Evaluate(admission, risky);
    REQUIRE_FALSE(defaultDecision.IsRuntimeSafe());

    PartyQuestRuntimeSafetyProfile profile;
    profile.HasVerifiedNativeAdapter = true;
    const auto plan = PartyQuestRuntimeSafetyPolicy::BuildApplyPlan(admission, risky, profile);
    REQUIRE(plan.Safety.Status == PartyQuestRuntimeSafetyStatus::RuntimeSafe);
    REQUIRE(plan.Safety.Reason == PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter);
    REQUIRE(plan.Safety.IsRuntimeSafe());
    REQUIRE(HasPartyQuestApplyAction(plan.Actions, PartyQuestApplyAction::AdapterManaged));

    // Even a verified classification is only a dry-run in this milestone.
    REQUIRE(plan.DryRunOnly);
    REQUIRE_FALSE(plan.WouldMutateQuestStage());
}
