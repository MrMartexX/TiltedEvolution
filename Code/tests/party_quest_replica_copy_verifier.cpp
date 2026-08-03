#include <Structs/Skyrim/PartyQuestReplicaFiles.h>

#include <catch2/catch.hpp>

namespace
{
PartyQuestReplicaCopyPlan BuildVerificationPlan()
{
    PartyQuestReplicaCopyPlan plan;
    plan.Status = PartyQuestReplicaCopyPlanStatus::Ready;
    plan.Operations = {
        {PartyQuestReplicaFileKind::SkyrimSave, "source/A.ess", "dest/A.ess", 100, 0xA1},
        {PartyQuestReplicaFileKind::SkseCosave, "source/A.skse", "dest/A.skse", 20, 0xA2}
    };
    return plan;
}
} // namespace

TEST_CASE("Replica copy verifier requires every planned file to match size and digest", "[quest.party-state.replica-files]")
{
    PartyQuestReplicaCopyVerifier verifier(BuildVerificationPlan());
    REQUIRE(verifier.IsValidPlan());
    REQUIRE_FALSE(verifier.IsFullyVerified());
    REQUIRE(verifier.GetExpectedCount() == 2);
    REQUIRE(verifier.GetVerifiedCount() == 0);

    REQUIRE(verifier.Observe("dest/A.ess", 100, 0xA1) ==
        PartyQuestReplicaCopyVerificationStatus::Accepted);
    REQUIRE(verifier.GetVerifiedCount() == 1);
    REQUIRE_FALSE(verifier.IsFullyVerified());

    REQUIRE(verifier.Observe("dest/A.skse", 20, 0xA2) ==
        PartyQuestReplicaCopyVerificationStatus::AllVerified);
    REQUIRE(verifier.GetVerifiedCount() == 2);
    REQUIRE(verifier.IsFullyVerified());
}

TEST_CASE("Replica copy verifier does not accept size or digest mismatches", "[quest.party-state.replica-files]")
{
    SECTION("size mismatch")
    {
        PartyQuestReplicaCopyVerifier verifier(BuildVerificationPlan());
        REQUIRE(verifier.Observe("dest/A.ess", 99, 0xA1) ==
            PartyQuestReplicaCopyVerificationStatus::SizeMismatch);
        REQUIRE(verifier.GetVerifiedCount() == 0);
        REQUIRE_FALSE(verifier.IsFullyVerified());
    }

    SECTION("digest mismatch")
    {
        PartyQuestReplicaCopyVerifier verifier(BuildVerificationPlan());
        REQUIRE(verifier.Observe("dest/A.ess", 100, 0xBAD) ==
            PartyQuestReplicaCopyVerificationStatus::DigestMismatch);
        REQUIRE(verifier.GetVerifiedCount() == 0);
        REQUIRE_FALSE(verifier.IsFullyVerified());
    }
}

TEST_CASE("Replica copy verifier is idempotent but rejects conflicting duplicate evidence", "[quest.party-state.replica-files]")
{
    PartyQuestReplicaCopyVerifier verifier(BuildVerificationPlan());
    REQUIRE(verifier.Observe("dest/A.ess", 100, 0xA1) ==
        PartyQuestReplicaCopyVerificationStatus::Accepted);

    REQUIRE(verifier.Observe("dest/A.ess", 100, 0xA1) ==
        PartyQuestReplicaCopyVerificationStatus::Duplicate);
    REQUIRE(verifier.GetVerifiedCount() == 1);

    REQUIRE(verifier.Observe("dest/A.ess", 100, 0xA3) ==
        PartyQuestReplicaCopyVerificationStatus::DuplicateConflict);
    REQUIRE(verifier.GetVerifiedCount() == 1);
}

TEST_CASE("Replica copy verifier rejects unknown destinations and invalid plans", "[quest.party-state.replica-files]")
{
    PartyQuestReplicaCopyVerifier verifier(BuildVerificationPlan());
    REQUIRE(verifier.Observe("dest/unknown.bin", 1, 1) ==
        PartyQuestReplicaCopyVerificationStatus::UnknownDestination);

    PartyQuestReplicaCopyPlan invalid;
    invalid.Status = PartyQuestReplicaCopyPlanStatus::InvalidSource;
    PartyQuestReplicaCopyVerifier invalidVerifier(invalid);
    REQUIRE_FALSE(invalidVerifier.IsValidPlan());
    REQUIRE(invalidVerifier.Observe("dest/A.ess", 100, 0xA1) ==
        PartyQuestReplicaCopyVerificationStatus::InvalidPlan);
}

TEST_CASE("Replica copy verifier canonicalizes destination paths", "[quest.party-state.replica-files]")
{
    PartyQuestReplicaCopyVerifier verifier(BuildVerificationPlan());
    REQUIRE(verifier.Observe("dest/sub/../A.ess", 100, 0xA1) ==
        PartyQuestReplicaCopyVerificationStatus::Accepted);
}
