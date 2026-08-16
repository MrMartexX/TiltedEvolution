#include <Structs/Skyrim/PartyQuestRuntimeCanonicalInbox.h>

#include <catch2/catch.hpp>

namespace
{
const PartyQuestCampaignId kCampaignA{
    0xC101C102C103C104ull,
    0xC105C106C107C108ull};
const PartyQuestCampaignId kCampaignB{
    0xC201C202C203C204ull,
    0xC205C206C207C208ull};
const GameId kQuestId(102, 0xC100);

PartyQuestRuntimeCanonicalCandidate BuildCandidate(
    const PartyQuestCampaignId& acCampaignId,
    uint64_t aTransactionId,
    uint64_t aWorldRevision,
    uint64_t aQuestRevision,
    uint16_t aStage)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = kQuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = aStage;
    snapshot.Revision = aQuestRevision;
    snapshot.InitiatorPlayerId = 27;
    snapshot.CompletedStages = {10, aStage};
    snapshot.Objectives = {{aStage, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();

    PartyQuestRuntimeCanonicalCandidate candidate;
    candidate.CampaignId = acCampaignId;
    candidate.TransactionId = aTransactionId;
    candidate.WorldRevision = aWorldRevision;
    candidate.CanonicalSnapshot = std::move(snapshot);
    return candidate;
}
} // namespace

TEST_CASE(
    "Runtime canonical inbox requires an exact campaign binding",
    "[quest.party-state.runtime-canonical-inbox][runtime-authority]")
{
    PartyQuestRuntimeCanonicalInbox inbox;
    const auto candidate = BuildCandidate(kCampaignA, 42001, 52001, 8, 20);

    REQUIRE(inbox.Observe(candidate) ==
        PartyQuestRuntimeCanonicalObserveStatus::InvalidInput);
    REQUIRE(inbox.GetPendingQuestCount() == 0);
    REQUIRE_FALSE(inbox.BindCampaign({}));
    REQUIRE(inbox.BindCampaign(kCampaignA));

    auto wrongCampaign = candidate;
    wrongCampaign.CampaignId = kCampaignB;
    REQUIRE(inbox.Observe(wrongCampaign) ==
        PartyQuestRuntimeCanonicalObserveStatus::CampaignMismatch);
    REQUIRE(inbox.GetPendingQuestCount() == 0);
    REQUIRE(inbox.GetRememberedTransactionCount() == 0);
}

TEST_CASE(
    "Runtime canonical inbox preserves exact transaction identity",
    "[quest.party-state.runtime-canonical-inbox][runtime-authority]")
{
    PartyQuestRuntimeCanonicalInbox inbox;
    REQUIRE(inbox.BindCampaign(kCampaignA));

    const auto candidate = BuildCandidate(kCampaignA, 42002, 52002, 9, 30);
    REQUIRE(inbox.Observe(candidate) ==
        PartyQuestRuntimeCanonicalObserveStatus::Accepted);
    REQUIRE(inbox.Observe(candidate) ==
        PartyQuestRuntimeCanonicalObserveStatus::Duplicate);

    auto conflict = candidate;
    conflict.WorldRevision += 1;
    conflict.CanonicalSnapshot.CurrentStage = 40;
    conflict.CanonicalSnapshot.CompletedStages.push_back(40);
    conflict.CanonicalSnapshot.Revision += 1;
    conflict.CanonicalSnapshot.Canonicalize();
    REQUIRE(inbox.Observe(conflict) ==
        PartyQuestRuntimeCanonicalObserveStatus::TransactionConflict);

    const auto* latest = inbox.FindLatest(kQuestId);
    REQUIRE(latest != nullptr);
    REQUIRE(*latest == candidate);
    REQUIRE(inbox.GetRememberedTransactionCount() == 1);
}

TEST_CASE(
    "Runtime canonical inbox only supersedes with monotonic world and quest revisions",
    "[quest.party-state.runtime-canonical-inbox][revision]")
{
    PartyQuestRuntimeCanonicalInbox inbox;
    REQUIRE(inbox.BindCampaign(kCampaignA));

    const auto first = BuildCandidate(kCampaignA, 42003, 52003, 10, 30);
    REQUIRE(inbox.Observe(first) ==
        PartyQuestRuntimeCanonicalObserveStatus::Accepted);

    const auto newer = BuildCandidate(kCampaignA, 42004, 52004, 11, 40);
    REQUIRE(inbox.Observe(newer) ==
        PartyQuestRuntimeCanonicalObserveStatus::Superseded);
    REQUIRE(*inbox.FindLatest(kQuestId) == newer);

    const auto staleWorld = BuildCandidate(kCampaignA, 42005, 52004, 12, 50);
    REQUIRE(inbox.Observe(staleWorld) ==
        PartyQuestRuntimeCanonicalObserveStatus::Stale);
    REQUIRE(*inbox.FindLatest(kQuestId) == newer);

    const auto staleQuest = BuildCandidate(kCampaignA, 42006, 52005, 11, 50);
    REQUIRE(inbox.Observe(staleQuest) ==
        PartyQuestRuntimeCanonicalObserveStatus::Stale);
    REQUIRE(*inbox.FindLatest(kQuestId) == newer);

    // Stale transaction provenance is still remembered. Reusing the same id
    // with a different payload must conflict rather than getting a second try.
    auto staleConflict = staleWorld;
    staleConflict.WorldRevision = 52006;
    staleConflict.CanonicalSnapshot.Revision = 13;
    staleConflict.CanonicalSnapshot.Canonicalize();
    REQUIRE(inbox.Observe(staleConflict) ==
        PartyQuestRuntimeCanonicalObserveStatus::TransactionConflict);
    REQUIRE(inbox.GetRememberedTransactionCount() == 4);
}

TEST_CASE(
    "Runtime canonical inbox campaign switch fences old candidates and transaction history",
    "[quest.party-state.runtime-canonical-inbox][lifecycle]")
{
    PartyQuestRuntimeCanonicalInbox inbox;
    REQUIRE(inbox.BindCampaign(kCampaignA));
    const auto oldCandidate = BuildCandidate(kCampaignA, 42007, 52007, 13, 60);
    REQUIRE(inbox.Observe(oldCandidate) ==
        PartyQuestRuntimeCanonicalObserveStatus::Accepted);
    REQUIRE(inbox.GetPendingQuestCount() == 1);
    REQUIRE(inbox.GetRememberedTransactionCount() == 1);

    REQUIRE(inbox.BindCampaign(kCampaignB));
    REQUIRE(inbox.GetCampaignId() == kCampaignB);
    REQUIRE(inbox.GetPendingQuestCount() == 0);
    REQUIRE(inbox.GetRememberedTransactionCount() == 0);
    REQUIRE(inbox.FindLatest(kQuestId) == nullptr);

    auto newCandidate = oldCandidate;
    newCandidate.CampaignId = kCampaignB;
    REQUIRE(inbox.Observe(newCandidate) ==
        PartyQuestRuntimeCanonicalObserveStatus::Accepted);

    inbox.Reset();
    REQUIRE_FALSE(inbox.GetCampaignId().IsValid());
    REQUIRE(inbox.GetPendingQuestCount() == 0);
    REQUIRE(inbox.GetRememberedTransactionCount() == 0);
}
