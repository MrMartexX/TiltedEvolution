#include <Structs/Skyrim/PartyQuestRuntimeCanonicalInbox.h>
#include <Structs/Skyrim/PartyQuestRepair.h>

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

PartyQuestReplica BuildPublishedReplica(
    const PartyQuestRuntimeCanonicalCandidate& acCandidate)
{
    PartyQuestReplica replica;
    replica.ObserveLocalSnapshot(acCandidate.CanonicalSnapshot);
    replica.SetObservedWorldRevision(acCandidate.WorldRevision);
    return replica;
}
} // namespace

TEST_CASE(
    "Runtime canonical inbox requires an exact campaign binding",
    "[quest.party-state.runtime-canonical-inbox][runtime-authority]")
{
    PartyQuestRuntimeCanonicalInbox inbox;
    const auto candidate = BuildCandidate(kCampaignA, 42001, 52001, 8, 20);
    const auto publishedReplica = BuildPublishedReplica(candidate);

    REQUIRE(inbox.Observe(candidate, publishedReplica) ==
        PartyQuestRuntimeCanonicalObserveStatus::InvalidInput);
    REQUIRE(inbox.GetPendingQuestCount() == 0);
    REQUIRE_FALSE(inbox.BindCampaign({}));
    REQUIRE(inbox.BindCampaign(kCampaignA));

    auto wrongCampaign = candidate;
    wrongCampaign.CampaignId = kCampaignB;
    REQUIRE(inbox.Observe(wrongCampaign, publishedReplica) ==
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
    const auto publishedReplica = BuildPublishedReplica(candidate);
    REQUIRE(inbox.Observe(candidate, publishedReplica) ==
        PartyQuestRuntimeCanonicalObserveStatus::Accepted);
    REQUIRE(inbox.Observe(candidate, publishedReplica) ==
        PartyQuestRuntimeCanonicalObserveStatus::Duplicate);

    auto conflict = candidate;
    conflict.WorldRevision += 1;
    conflict.CanonicalSnapshot.CurrentStage = 40;
    conflict.CanonicalSnapshot.CompletedStages.push_back(40);
    conflict.CanonicalSnapshot.Revision += 1;
    conflict.CanonicalSnapshot.Canonicalize();
    REQUIRE(inbox.Observe(conflict, publishedReplica) ==
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
    const auto firstReplica = BuildPublishedReplica(first);
    REQUIRE(inbox.Observe(first, firstReplica) ==
        PartyQuestRuntimeCanonicalObserveStatus::Accepted);

    const auto newer = BuildCandidate(kCampaignA, 42004, 52004, 11, 40);
    const auto newerReplica = BuildPublishedReplica(newer);
    REQUIRE(inbox.Observe(newer, newerReplica) ==
        PartyQuestRuntimeCanonicalObserveStatus::Superseded);
    REQUIRE(*inbox.FindLatest(kQuestId) == newer);

    const auto staleWorld = BuildCandidate(kCampaignA, 42005, 52004, 12, 50);
    const auto staleWorldReplica = BuildPublishedReplica(staleWorld);
    REQUIRE(inbox.Observe(staleWorld, staleWorldReplica) ==
        PartyQuestRuntimeCanonicalObserveStatus::Stale);
    REQUIRE(*inbox.FindLatest(kQuestId) == newer);

    const auto staleQuest = BuildCandidate(kCampaignA, 42006, 52005, 11, 50);
    const auto staleQuestReplica = BuildPublishedReplica(staleQuest);
    REQUIRE(inbox.Observe(staleQuest, staleQuestReplica) ==
        PartyQuestRuntimeCanonicalObserveStatus::Stale);
    REQUIRE(*inbox.FindLatest(kQuestId) == newer);

    // Stale transaction provenance is still remembered. Reusing the same id
    // with a different payload must conflict rather than getting a second try.
    auto staleConflict = staleWorld;
    staleConflict.WorldRevision = 52006;
    staleConflict.CanonicalSnapshot.Revision = 13;
    staleConflict.CanonicalSnapshot.Canonicalize();
    REQUIRE(inbox.Observe(staleConflict, staleWorldReplica) ==
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
    const auto oldReplica = BuildPublishedReplica(oldCandidate);
    REQUIRE(inbox.Observe(oldCandidate, oldReplica) ==
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
    const auto newReplica = BuildPublishedReplica(newCandidate);
    REQUIRE(inbox.Observe(newCandidate, newReplica) ==
        PartyQuestRuntimeCanonicalObserveStatus::Accepted);

    inbox.Reset();
    REQUIRE_FALSE(inbox.GetCampaignId().IsValid());
    REQUIRE(inbox.GetPendingQuestCount() == 0);
    REQUIRE(inbox.GetRememberedTransactionCount() == 0);
}

TEST_CASE(
    "Runtime canonical inbox rejects a cached duplicate that is no longer the published replica head",
    "[quest.party-state.runtime-canonical-inbox][revision][replay]")
{
    PartyQuestRuntimeCanonicalInbox inbox;
    REQUIRE(inbox.BindCampaign(kCampaignA));

    const auto oldCandidate = BuildCandidate(kCampaignA, 42008, 52008, 14, 60);
    const auto oldReplica = BuildPublishedReplica(oldCandidate);
    REQUIRE(inbox.Observe(oldCandidate, oldReplica) ==
        PartyQuestRuntimeCanonicalObserveStatus::Accepted);

    // A verified repair starts a fresh runtime-evidence epoch. Protocol-level
    // duplicate caches may still remember the old exact transaction, but the
    // repaired published replica has advanced and is now the only valid head.
    inbox.Reset();
    REQUIRE(inbox.BindCampaign(kCampaignA));

    const auto repairedHead = BuildCandidate(kCampaignA, 42009, 52009, 15, 70);
    const auto repairedReplica = BuildPublishedReplica(repairedHead);
    REQUIRE(inbox.Observe(oldCandidate, repairedReplica) ==
        PartyQuestRuntimeCanonicalObserveStatus::ReplicaHeadMismatch);
    REQUIRE(inbox.GetPendingQuestCount() == 0);
    REQUIRE(inbox.GetRememberedTransactionCount() == 0);

    // Even a forged/replayed packet that preserves the current world revision
    // must match the exact published canonical snapshot, not only the revision.
    auto sameWorldWrongSnapshot = oldCandidate;
    sameWorldWrongSnapshot.WorldRevision = repairedHead.WorldRevision;
    REQUIRE(inbox.Observe(sameWorldWrongSnapshot, repairedReplica) ==
        PartyQuestRuntimeCanonicalObserveStatus::ReplicaHeadMismatch);
    REQUIRE(inbox.GetPendingQuestCount() == 0);
    REQUIRE(inbox.GetRememberedTransactionCount() == 0);
}
