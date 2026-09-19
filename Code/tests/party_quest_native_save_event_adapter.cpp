#include <Structs/Skyrim/PartyQuestNativeSaveEventAdapter.h>
#include <Structs/Skyrim/PartyQuestAsyncSaveEventRouter.h>

#include <catch2/catch.hpp>

#include <array>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace
{
using Buffer = std::array<uint8_t, PartyQuestNativeSaveEventAdapter::kStructSize>;
using RetiredBuffer = std::array<uint8_t, PartyQuestNativeSaveEventAdapter::kRequestRetiredStructSize>;

std::string SaveName(uint64_t aTransaction, uint64_t aRevision, uint64_t aNonce)
{
    std::array<char, 96> text{};
    const int written = std::snprintf(
        text.data(),
        text.size(),
        "STR_PreRepair_T%016llX_R%016llX_A%016llX",
        static_cast<unsigned long long>(aTransaction),
        static_cast<unsigned long long>(aRevision),
        static_cast<unsigned long long>(aNonce));
    REQUIRE(written > 0);
    REQUIRE(static_cast<size_t>(written) < text.size());
    return text.data();
}

PartyQuestAsyncSaveRequestIdentity Identity();
Buffer Event(
    const PartyQuestAsyncSaveRequestIdentity& acIdentity,
    uint8_t aArtifact = 1,
    uint8_t aPhase = 1,
    uint8_t aOutcome = 1,
    uint32_t aNativeError = 0);
RetiredBuffer RetiredEvent(
    const PartyQuestAsyncSaveRequestIdentity& acIdentity,
    uint8_t aOutcome = 1,
    uint32_t aNativeError = 0);

TEST_CASE("Native save router releases completion only after matching PQS4")
{
    PartyQuestAsyncSaveContract contract;
    PartyQuestAsyncSaveFinalizationGate gate;
    const auto identity = Identity();
    REQUIRE(gate.BeginCoordinated(contract, identity, 100, false, false).Status ==
            PartyQuestAsyncSaveFinalizationStatus::Pending);

    const std::array events{
        Event(identity, 1, 1, 1),
        Event(identity, 2, 1, 1),
        Event(identity, 1, 2, 1),
        Event(identity, 2, 2, 1)};
    uint64_t now = 101;
    for (size_t index = 0; index < events.size(); ++index)
    {
        auto routed = PartyQuestAsyncSaveEventRouter::ApplyArtifact(
            events[index].data(), events[index].size(), identity,
            contract, gate, now++);
        REQUIRE(routed.Status == PartyQuestAsyncSaveEventRouteStatus::Applied);
        REQUIRE_FALSE(routed.Finalization.Completion.has_value());
        REQUIRE(routed.Finalization.Status ==
            (index + 1 == events.size() ?
                PartyQuestAsyncSaveFinalizationStatus::AwaitingRetirement :
                PartyQuestAsyncSaveFinalizationStatus::Pending));
    }

    const auto retiredPayload = RetiredEvent(identity, 1);
    auto retired = PartyQuestAsyncSaveEventRouter::ApplyRetirement(
        retiredPayload.data(), retiredPayload.size(), identity,
        contract, gate);
    REQUIRE(retired.Status == PartyQuestAsyncSaveEventRouteStatus::Applied);
    REQUIRE(retired.Finalization.Status ==
            PartyQuestAsyncSaveFinalizationStatus::Finalized);
    REQUIRE(retired.Finalization.RetirementApplied);
    REQUIRE(retired.Finalization.Completion.has_value());
    REQUIRE(retired.Finalization.Completion->Matches(identity));
}

TEST_CASE("Native save router consumes early drain proof without success")
{
    PartyQuestAsyncSaveContract contract;
    PartyQuestAsyncSaveFinalizationGate gate;
    const auto identity = Identity();
    REQUIRE(gate.BeginCoordinated(contract, identity, 100, false, false).Status ==
            PartyQuestAsyncSaveFinalizationStatus::Pending);

    const auto retiredPayload = RetiredEvent(identity, 2, 5);
    auto retired = PartyQuestAsyncSaveEventRouter::ApplyRetirement(
        retiredPayload.data(), retiredPayload.size(), identity,
        contract, gate);
    REQUIRE(retired.Status == PartyQuestAsyncSaveEventRouteStatus::Applied);
    REQUIRE(retired.Finalization.Status ==
            PartyQuestAsyncSaveFinalizationStatus::ProtocolViolationRetired);
    REQUIRE(retired.Finalization.RetirementApplied);
    REQUIRE_FALSE(retired.Finalization.Completion.has_value());
}

TEST_CASE("Native save router revokes quarantined completion on late failure")
{
    PartyQuestAsyncSaveContract contract;
    PartyQuestAsyncSaveFinalizationGate gate;
    const auto identity = Identity();
    REQUIRE(gate.BeginCoordinated(contract, identity, 100, false, false).Status ==
            PartyQuestAsyncSaveFinalizationStatus::Pending);

    const std::array successes{
        Event(identity, 1, 1, 1),
        Event(identity, 2, 1, 1),
        Event(identity, 1, 2, 1),
        Event(identity, 2, 2, 1)};
    uint64_t now = 101;
    for (const auto& payload : successes)
    {
        auto routed = PartyQuestAsyncSaveEventRouter::ApplyArtifact(
            payload.data(), payload.size(), identity, contract, gate, now++);
        REQUIRE(routed.Status == PartyQuestAsyncSaveEventRouteStatus::Applied);
        REQUIRE_FALSE(routed.Finalization.Completion.has_value());
    }

    const auto lateFailure = Event(identity, 1, 1, 2, 5);
    auto failed = PartyQuestAsyncSaveEventRouter::ApplyArtifact(
        lateFailure.data(), lateFailure.size(), identity,
        contract, gate, now);
    REQUIRE(failed.Status == PartyQuestAsyncSaveEventRouteStatus::Applied);
    REQUIRE(failed.Finalization.Status ==
            PartyQuestAsyncSaveFinalizationStatus::ProtocolViolation);
    REQUIRE_FALSE(failed.Finalization.Completion.has_value());

    const auto contradictoryRetirement = RetiredEvent(identity, 1);
    auto retired = PartyQuestAsyncSaveEventRouter::ApplyRetirement(
        contradictoryRetirement.data(), contradictoryRetirement.size(),
        identity, contract, gate);
    REQUIRE(retired.Finalization.Status ==
            PartyQuestAsyncSaveFinalizationStatus::ProtocolViolationRetired);
    REQUIRE(retired.Finalization.RetirementApplied);
    REQUIRE_FALSE(retired.Finalization.Completion.has_value());
}

TEST_CASE("Native save router rejects stale and malformed callbacks unchanged")
{
    PartyQuestAsyncSaveContract contract;
    PartyQuestAsyncSaveFinalizationGate gate;
    const auto identity = Identity();
    REQUIRE(gate.BeginCoordinated(contract, identity, 100, false, false).Status ==
            PartyQuestAsyncSaveFinalizationStatus::Pending);

    auto staleIdentity = identity;
    ++staleIdentity.RuntimeGeneration;
    const auto stalePayload = Event(staleIdentity);
    const auto stale = PartyQuestAsyncSaveEventRouter::ApplyArtifact(
        stalePayload.data(), stalePayload.size(), identity,
        contract, gate, 101);
    REQUIRE(stale.Status == PartyQuestAsyncSaveEventRouteStatus::IdentityMismatch);

    const auto validPayload = Event(identity);
    const auto malformed = PartyQuestAsyncSaveEventRouter::ApplyArtifact(
        validPayload.data(), validPayload.size() - 1, identity,
        contract, gate, 102);
    REQUIRE(malformed.Status == PartyQuestAsyncSaveEventRouteStatus::Malformed);

    REQUIRE(contract.Begin(staleIdentity, 103, false, false).Status ==
            PartyQuestAsyncSaveContractStatus::Busy);
}

PartyQuestAsyncSaveRequestIdentity Identity()
{
    PartyQuestAsyncSaveRequestIdentity identity;
    identity.CampaignId = {0x1111222233334444ull, 0x5555666677778888ull};
    identity.PlayerProfileId = {0x9999AAAABBBBCCCCull, 0xDDDDEEEEFFFF0001ull};
    identity.RuntimeGeneration = 10;
    identity.TransactionId = 20;
    identity.TargetWorldRevision = 30;
    identity.CaptureEpochId = 40;
    identity.AttemptNonce = 50;
    identity.SaveName = SaveName(
        identity.TransactionId,
        identity.TargetWorldRevision,
        identity.AttemptNonce);
    REQUIRE(identity.IsValid());
    return identity;
}

template <size_t Size>
void WriteU32(std::array<uint8_t, Size>& aBuffer, size_t aOffset, uint32_t aValue)
{
    for (size_t index = 0; index < sizeof(aValue); ++index)
        aBuffer[aOffset + index] = static_cast<uint8_t>(aValue >> (index * 8));
}

template <size_t Size>
void WriteU64(std::array<uint8_t, Size>& aBuffer, size_t aOffset, uint64_t aValue)
{
    for (size_t index = 0; index < sizeof(aValue); ++index)
        aBuffer[aOffset + index] = static_cast<uint8_t>(aValue >> (index * 8));
}

template <size_t Size>
void WriteId(std::array<uint8_t, Size>& aBuffer, size_t aOffset, uint64_t aHigh, uint64_t aLow)
{
    std::array<char, 33> text{};
    const int written = std::snprintf(
        text.data(),
        text.size(),
        "%016llX%016llX",
        static_cast<unsigned long long>(aHigh),
        static_cast<unsigned long long>(aLow));
    REQUIRE(written == 32);
    std::memcpy(aBuffer.data() + aOffset, text.data(), text.size());
}

Buffer Event(
    const PartyQuestAsyncSaveRequestIdentity& acIdentity,
    uint8_t aArtifact,
    uint8_t aPhase,
    uint8_t aOutcome,
    uint32_t aNativeError)
{
    Buffer buffer{};
    WriteU32(buffer, 0, PartyQuestNativeSaveEventAdapter::kAbiVersion);
    WriteU32(
        buffer,
        4,
        static_cast<uint32_t>(PartyQuestNativeSaveEventAdapter::kStructSize));
    WriteU64(buffer, 8, acIdentity.RuntimeGeneration);
    WriteU64(buffer, 16, acIdentity.TransactionId);
    WriteU64(buffer, 24, acIdentity.TargetWorldRevision);
    WriteU64(buffer, 32, acIdentity.CaptureEpochId);
    WriteU64(buffer, 40, acIdentity.AttemptNonce);
    WriteId(buffer, 48, acIdentity.CampaignId.High, acIdentity.CampaignId.Low);
    WriteId(
        buffer,
        81,
        acIdentity.PlayerProfileId.High,
        acIdentity.PlayerProfileId.Low);
    REQUIRE(acIdentity.SaveName.size() < 96);
    std::memcpy(
        buffer.data() + 114,
        acIdentity.SaveName.c_str(),
        acIdentity.SaveName.size() + 1);
    buffer[216] = aArtifact;
    buffer[217] = aPhase;
    buffer[218] = aOutcome;
    WriteU32(buffer, 224, aNativeError);
    return buffer;
}

RetiredBuffer RetiredEvent(
    const PartyQuestAsyncSaveRequestIdentity& acIdentity,
    uint8_t aOutcome,
    uint32_t aNativeError)
{
    RetiredBuffer buffer{};
    WriteU32(buffer, 0, PartyQuestNativeSaveEventAdapter::kAbiVersion);
    WriteU32(
        buffer,
        4,
        static_cast<uint32_t>(PartyQuestNativeSaveEventAdapter::kRequestRetiredStructSize));
    WriteU64(buffer, 8, acIdentity.RuntimeGeneration);
    WriteU64(buffer, 16, acIdentity.TransactionId);
    WriteU64(buffer, 24, acIdentity.TargetWorldRevision);
    WriteU64(buffer, 32, acIdentity.CaptureEpochId);
    WriteU64(buffer, 40, acIdentity.AttemptNonce);
    WriteId(buffer, 48, acIdentity.CampaignId.High, acIdentity.CampaignId.Low);
    WriteId(
        buffer,
        81,
        acIdentity.PlayerProfileId.High,
        acIdentity.PlayerProfileId.Low);
    REQUIRE(acIdentity.SaveName.size() < 96);
    std::memcpy(
        buffer.data() + 114,
        acIdentity.SaveName.c_str(),
        acIdentity.SaveName.size() + 1);
    buffer[216] = aOutcome;
    WriteU32(buffer, 220, aNativeError);
    return buffer;
}

PartyQuestNativeSaveEventAdapterResult Apply(
    PartyQuestAsyncSaveContract& aContract,
    const PartyQuestAsyncSaveRequestIdentity& acReservedIdentity,
    const Buffer& acBuffer,
    uint64_t aNowMs)
{
    return PartyQuestNativeSaveEventAdapter::ApplyTrustedProviderEvent(
        acBuffer.data(),
        acBuffer.size(),
        acReservedIdentity,
        aContract,
        aNowMs);
}

PartyQuestNativeSaveEventAdapterResult ApplyRetired(
    PartyQuestAsyncSaveContract& aContract,
    const PartyQuestAsyncSaveRequestIdentity& acReservedIdentity,
    const RetiredBuffer& acBuffer)
{
    return PartyQuestNativeSaveEventAdapter::ApplyTrustedProviderRetirement(
        acBuffer.data(),
        acBuffer.size(),
        acReservedIdentity,
        aContract);
}

void Begin(PartyQuestAsyncSaveContract& aContract, const PartyQuestAsyncSaveRequestIdentity& acIdentity)
{
    REQUIRE(
        aContract.Begin(acIdentity, 100, false, false).Status ==
        PartyQuestAsyncSaveContractStatus::Pending);
}
} // namespace

TEST_CASE("Native save event ABI v2 decoder rejects structural ambiguity")
{
    const auto identity = Identity();
    const auto valid = Event(identity);

    REQUIRE(
        PartyQuestNativeSaveEventAdapter::Decode(nullptr, 0).Status ==
        PartyQuestNativeSaveEventDecodeStatus::NullBuffer);
    REQUIRE(
        PartyQuestNativeSaveEventAdapter::Decode(valid.data(), 0).Status ==
        PartyQuestNativeSaveEventDecodeStatus::Truncated);
    REQUIRE(
        PartyQuestNativeSaveEventAdapter::Decode(valid.data(), valid.size() - 1).Status ==
        PartyQuestNativeSaveEventDecodeStatus::Truncated);

    std::vector<uint8_t> extended(valid.begin(), valid.end());
    extended.push_back(0);
    REQUIRE(
        PartyQuestNativeSaveEventAdapter::Decode(extended.data(), extended.size()).Status ==
        PartyQuestNativeSaveEventDecodeStatus::TrailingBytes);

    SECTION("version")
    {
        auto malformed = valid;
        WriteU32(malformed, 0, 1);
        REQUIRE(
            PartyQuestNativeSaveEventAdapter::Decode(malformed.data(), malformed.size()).Status ==
            PartyQuestNativeSaveEventDecodeStatus::UnsupportedVersion);
    }
    SECTION("declared size")
    {
        auto malformed = valid;
        WriteU32(malformed, 4, 231);
        REQUIRE(
            PartyQuestNativeSaveEventAdapter::Decode(malformed.data(), malformed.size()).Status ==
            PartyQuestNativeSaveEventDecodeStatus::InvalidStructSize);
    }
    SECTION("artifact")
    {
        auto malformed = valid;
        malformed[216] = 3;
        REQUIRE(
            PartyQuestNativeSaveEventAdapter::Decode(malformed.data(), malformed.size()).Status ==
            PartyQuestNativeSaveEventDecodeStatus::InvalidArtifact);
    }
    SECTION("phase")
    {
        auto malformed = valid;
        malformed[217] = 0;
        REQUIRE(
            PartyQuestNativeSaveEventAdapter::Decode(malformed.data(), malformed.size()).Status ==
            PartyQuestNativeSaveEventDecodeStatus::InvalidPhase);
    }
    SECTION("outcome")
    {
        auto malformed = valid;
        malformed[218] = 9;
        REQUIRE(
            PartyQuestNativeSaveEventAdapter::Decode(malformed.data(), malformed.size()).Status ==
            PartyQuestNativeSaveEventDecodeStatus::InvalidOutcome);
    }
    SECTION("reserved identity bytes")
    {
        auto malformed = valid;
        malformed[210] = 1;
        REQUIRE(
            PartyQuestNativeSaveEventAdapter::Decode(malformed.data(), malformed.size()).Status ==
            PartyQuestNativeSaveEventDecodeStatus::NonZeroReserved);
    }
    SECTION("reserved event bytes")
    {
        auto malformed = valid;
        malformed[223] = 1;
        REQUIRE(
            PartyQuestNativeSaveEventAdapter::Decode(malformed.data(), malformed.size()).Status ==
            PartyQuestNativeSaveEventDecodeStatus::NonZeroReserved);
    }
    SECTION("reserved tail bytes")
    {
        auto malformed = valid;
        malformed[231] = 1;
        REQUIRE(
            PartyQuestNativeSaveEventAdapter::Decode(malformed.data(), malformed.size()).Status ==
            PartyQuestNativeSaveEventDecodeStatus::NonZeroReserved);
    }
    SECTION("success with native error")
    {
        auto malformed = valid;
        WriteU32(malformed, 224, 5);
        REQUIRE(
            PartyQuestNativeSaveEventAdapter::Decode(malformed.data(), malformed.size()).Status ==
            PartyQuestNativeSaveEventDecodeStatus::InconsistentNativeError);
    }
}

TEST_CASE("Native save event ABI v2 decoder enforces bounded canonical strings")
{
    const auto identity = Identity();
    const auto valid = Event(identity);

    SECTION("campaign terminator")
    {
        auto malformed = valid;
        std::memset(malformed.data() + 48, 'A', 33);
        REQUIRE(
            PartyQuestNativeSaveEventAdapter::Decode(malformed.data(), malformed.size()).Status ==
            PartyQuestNativeSaveEventDecodeStatus::UnterminatedCampaignId);
    }
    SECTION("campaign format")
    {
        auto malformed = valid;
        malformed[48] = 'a';
        REQUIRE(
            PartyQuestNativeSaveEventAdapter::Decode(malformed.data(), malformed.size()).Status ==
            PartyQuestNativeSaveEventDecodeStatus::InvalidCampaignId);
    }
    SECTION("zero campaign is not a valid id")
    {
        auto malformed = valid;
        std::memset(malformed.data() + 48, '0', 32);
        REQUIRE(
            PartyQuestNativeSaveEventAdapter::Decode(malformed.data(), malformed.size()).Status ==
            PartyQuestNativeSaveEventDecodeStatus::InvalidCampaignId);
    }
    SECTION("profile terminator")
    {
        auto malformed = valid;
        std::memset(malformed.data() + 81, 'A', 33);
        REQUIRE(
            PartyQuestNativeSaveEventAdapter::Decode(malformed.data(), malformed.size()).Status ==
            PartyQuestNativeSaveEventDecodeStatus::UnterminatedPlayerProfileId);
    }
    SECTION("profile format")
    {
        auto malformed = valid;
        malformed[81] = '-';
        REQUIRE(
            PartyQuestNativeSaveEventAdapter::Decode(malformed.data(), malformed.size()).Status ==
            PartyQuestNativeSaveEventDecodeStatus::InvalidPlayerProfileId);
    }
    SECTION("save name terminator")
    {
        auto malformed = valid;
        std::memset(malformed.data() + 114, 'X', 96);
        REQUIRE(
            PartyQuestNativeSaveEventAdapter::Decode(malformed.data(), malformed.size()).Status ==
            PartyQuestNativeSaveEventDecodeStatus::UnterminatedSaveName);
    }
    SECTION("save name must match transaction revision and nonce")
    {
        auto malformed = valid;
        malformed[114] = 'X';
        REQUIRE(
            PartyQuestNativeSaveEventAdapter::Decode(malformed.data(), malformed.size()).Status ==
            PartyQuestNativeSaveEventDecodeStatus::InvalidIdentity);
    }
    SECTION("numeric identity fields remain subject to contract validation")
    {
        auto malformed = valid;
        WriteU64(malformed, 8, 0);
        REQUIRE(
            PartyQuestNativeSaveEventAdapter::Decode(malformed.data(), malformed.size()).Status ==
            PartyQuestNativeSaveEventDecodeStatus::InvalidIdentity);
    }
}

TEST_CASE("Native save event decoder supports unaligned input and owns translated values")
{
    const auto identity = Identity();
    const auto source = Event(identity, 2, 2, 2, 1234);
    std::vector<uint8_t> unaligned(source.size() + 1, 0xCC);
    std::memcpy(unaligned.data() + 1, source.data(), source.size());

    auto decoded = PartyQuestNativeSaveEventAdapter::Decode(
        unaligned.data() + 1,
        source.size());
    REQUIRE(decoded.Status == PartyQuestNativeSaveEventDecodeStatus::Decoded);
    REQUIRE(decoded.Event);
    REQUIRE(decoded.Event->Identity == identity);
    REQUIRE(decoded.Event->Artifact == PartyQuestAsyncSaveArtifact::SkseCosave);
    REQUIRE(decoded.Event->Phase == PartyQuestNativeSaveEventPhase::Published);
    REQUIRE(decoded.Event->Outcome == PartyQuestNativeSaveEventOutcome::Failed);
    REQUIRE(decoded.Event->NativeError == 1234);

    std::memset(unaligned.data() + 1, 0, source.size());
    REQUIRE(decoded.Event->Identity == identity);
    REQUIRE(decoded.Event->Identity.SaveName == identity.SaveName);
}

TEST_CASE("Native save events complete only after both close and publication phases")
{
    PartyQuestAsyncSaveContract contract;
    const auto identity = Identity();
    Begin(contract, identity);

    auto result = Apply(contract, identity, Event(identity, 1, 1), 101);
    REQUIRE(result.Status == PartyQuestNativeSaveEventAdapterStatus::Pending);
    REQUIRE(result.ContractResult);
    REQUIRE(result.ContractResult->SkyrimEssClosed);
    REQUIRE_FALSE(result.ContractResult->SkyrimEssPublished);
    REQUIRE_FALSE(result.ContractResult->Completion);

    result = Apply(contract, identity, Event(identity, 1, 2), 102);
    REQUIRE(result.Status == PartyQuestNativeSaveEventAdapterStatus::Pending);
    REQUIRE(result.ContractResult->SkyrimEssPublished);
    REQUIRE_FALSE(result.ContractResult->SkseCosaveClosed);
    REQUIRE_FALSE(result.ContractResult->Completion);

    result = Apply(contract, identity, Event(identity, 2, 1), 103);
    REQUIRE(result.Status == PartyQuestNativeSaveEventAdapterStatus::Pending);
    REQUIRE(result.ContractResult->SkseCosaveClosed);
    REQUIRE_FALSE(result.ContractResult->Completion);

    result = Apply(contract, identity, Event(identity, 2, 2), 104);
    REQUIRE(result.Status == PartyQuestNativeSaveEventAdapterStatus::Complete);
    REQUIRE(result.ContractResult);
    REQUIRE(result.ContractResult->Completion);
    REQUIRE(result.ContractResult->Completion->Matches(identity));

    result = Apply(contract, identity, Event(identity, 2, 2), 105);
    REQUIRE(result.Status == PartyQuestNativeSaveEventAdapterStatus::Duplicate);
    REQUIRE(result.ContractResult);
    REQUIRE_FALSE(result.ContractResult->Completion);
}

TEST_CASE("Native save event adapter exposes deterministic duplicate and ordering outcomes")
{
    const auto identity = Identity();

    SECTION("duplicate close and publication are idempotent")
    {
        PartyQuestAsyncSaveContract contract;
        Begin(contract, identity);
        REQUIRE(Apply(contract, identity, Event(identity, 1, 1), 101).Status ==
                PartyQuestNativeSaveEventAdapterStatus::Pending);
        REQUIRE(Apply(contract, identity, Event(identity, 1, 1), 102).Status ==
                PartyQuestNativeSaveEventAdapterStatus::Duplicate);
        REQUIRE(Apply(contract, identity, Event(identity, 1, 2), 103).Status ==
                PartyQuestNativeSaveEventAdapterStatus::Pending);
        REQUIRE(Apply(contract, identity, Event(identity, 1, 2), 104).Status ==
                PartyQuestNativeSaveEventAdapterStatus::Duplicate);
        REQUIRE(contract.Poll(105).Status == PartyQuestAsyncSaveContractStatus::Pending);
    }

    SECTION("publication before close fails closed")
    {
        PartyQuestAsyncSaveContract contract;
        Begin(contract, identity);
        const auto result = Apply(contract, identity, Event(identity, 2, 2), 101);
        REQUIRE(result.Status == PartyQuestNativeSaveEventAdapterStatus::Failed);
        REQUIRE(result.ContractResult);
        REQUIRE_FALSE(result.ContractResult->SkseCosavePublished);
        REQUIRE(result.ContractResult->CleanupRequired);
    }

    SECTION("success followed by contradictory failure fails while pending")
    {
        PartyQuestAsyncSaveContract contract;
        Begin(contract, identity);
        REQUIRE(Apply(contract, identity, Event(identity, 1, 1), 101).Status ==
                PartyQuestNativeSaveEventAdapterStatus::Pending);
        const auto failed = Apply(contract, identity, Event(identity, 1, 1, 2, 55), 102);
        REQUIRE(failed.Status == PartyQuestNativeSaveEventAdapterStatus::Failed);
        REQUIRE(failed.ContractResult->SkyrimEssClosed);
        REQUIRE_FALSE(failed.ContractResult->Completion);
    }
}

TEST_CASE("Authoritative native close and publication failures fail the active contract")
{
    const auto identity = Identity();
    for (const uint8_t artifact : {uint8_t{1}, uint8_t{2}})
    {
        for (const uint8_t phase : {uint8_t{1}, uint8_t{2}})
        {
            DYNAMIC_SECTION("artifact=" << static_cast<int>(artifact) << " phase=" << static_cast<int>(phase))
            {
                PartyQuestAsyncSaveContract contract;
                Begin(contract, identity);
                if (phase == 2)
                {
                    REQUIRE(Apply(contract, identity, Event(identity, artifact, 1), 101).Status ==
                            PartyQuestNativeSaveEventAdapterStatus::Pending);
                }
                const auto failed = Apply(
                    contract,
                    identity,
                    Event(identity, artifact, phase, 2, 5),
                    102);
                REQUIRE(failed.Status == PartyQuestNativeSaveEventAdapterStatus::Failed);
                REQUIRE(failed.ContractResult);
                REQUIRE(failed.ContractResult->Status == PartyQuestAsyncSaveContractStatus::Failed);
                REQUIRE(failed.ContractResult->CleanupRequired);
                REQUIRE_FALSE(failed.ContractResult->Completion);

                const auto lateSuccess = Apply(
                    contract,
                    identity,
                    Event(identity, artifact, phase),
                    103);
                REQUIRE(lateSuccess.Status == PartyQuestNativeSaveEventAdapterStatus::Failed);
            }
        }
    }
}

TEST_CASE("Native event adapter compares every owned request identity field")
{
    const auto reserved = Identity();
    std::vector<PartyQuestAsyncSaveRequestIdentity> mismatches;

    auto campaign = reserved;
    campaign.CampaignId.Low ^= 1;
    mismatches.push_back(campaign);
    auto profile = reserved;
    profile.PlayerProfileId.Low ^= 1;
    mismatches.push_back(profile);
    auto generation = reserved;
    ++generation.RuntimeGeneration;
    mismatches.push_back(generation);
    auto transaction = reserved;
    ++transaction.TransactionId;
    transaction.SaveName = SaveName(transaction.TransactionId, transaction.TargetWorldRevision, transaction.AttemptNonce);
    mismatches.push_back(transaction);
    auto revision = reserved;
    ++revision.TargetWorldRevision;
    revision.SaveName = SaveName(revision.TransactionId, revision.TargetWorldRevision, revision.AttemptNonce);
    mismatches.push_back(revision);
    auto epoch = reserved;
    ++epoch.CaptureEpochId;
    mismatches.push_back(epoch);
    auto nonce = reserved;
    ++nonce.AttemptNonce;
    nonce.SaveName = SaveName(nonce.TransactionId, nonce.TargetWorldRevision, nonce.AttemptNonce);
    mismatches.push_back(nonce);

    for (size_t index = 0; index < mismatches.size(); ++index)
    {
        DYNAMIC_SECTION("identity field " << index)
        {
            REQUIRE(mismatches[index].IsValid());
            PartyQuestAsyncSaveContract contract;
            Begin(contract, reserved);
            const auto result = Apply(contract, reserved, Event(mismatches[index]), 101);
            REQUIRE(result.Status == PartyQuestNativeSaveEventAdapterStatus::IdentityMismatch);
            REQUIRE_FALSE(result.ContractResult);
            const auto state = contract.Poll(102);
            REQUIRE(state.Status == PartyQuestAsyncSaveContractStatus::Pending);
            REQUIRE_FALSE(state.SkyrimEssClosed);
            REQUIRE_FALSE(state.SkseCosaveClosed);
        }
    }

    auto wrongSave = Event(reserved);
    wrongSave[114] = 'X';
    PartyQuestAsyncSaveContract contract;
    Begin(contract, reserved);
    const auto malformed = Apply(contract, reserved, wrongSave, 101);
    REQUIRE(malformed.Status == PartyQuestNativeSaveEventAdapterStatus::Malformed);
    REQUIRE(malformed.DecodeStatus == PartyQuestNativeSaveEventDecodeStatus::InvalidIdentity);
    REQUIRE(contract.Poll(102).Status == PartyQuestAsyncSaveContractStatus::Pending);
}

TEST_CASE("Late callbacks cannot cross request generations or campaign ABA")
{
    PartyQuestAsyncSaveContract contract;
    auto oldA = Identity();
    auto middleB = oldA;
    middleB.CampaignId.Low ^= 1;
    ++middleB.RuntimeGeneration;
    ++middleB.AttemptNonce;
    middleB.SaveName = SaveName(middleB.TransactionId, middleB.TargetWorldRevision, middleB.AttemptNonce);
    auto currentA = oldA;
    currentA.RuntimeGeneration += 2;
    currentA.AttemptNonce += 2;
    currentA.SaveName = SaveName(currentA.TransactionId, currentA.TargetWorldRevision, currentA.AttemptNonce);

    Begin(contract, oldA);
    REQUIRE(contract.Cancel(oldA).Status == PartyQuestAsyncSaveContractStatus::Cancelled);
    REQUIRE(contract.Retire(oldA).Status == PartyQuestAsyncSaveContractStatus::Inactive);
    REQUIRE(contract.Begin(middleB, 110, false, false).Status == PartyQuestAsyncSaveContractStatus::Pending);
    REQUIRE(contract.Cancel(middleB).Status == PartyQuestAsyncSaveContractStatus::Cancelled);
    REQUIRE(contract.Retire(middleB).Status == PartyQuestAsyncSaveContractStatus::Inactive);
    REQUIRE(contract.Begin(currentA, 120, false, false).Status == PartyQuestAsyncSaveContractStatus::Pending);

    const auto late = Apply(contract, currentA, Event(oldA), 121);
    REQUIRE(late.Status == PartyQuestNativeSaveEventAdapterStatus::IdentityMismatch);
    REQUIRE(contract.Poll(122).Status == PartyQuestAsyncSaveContractStatus::Pending);
    REQUIRE(contract.Begin(oldA, 123, false, false).Status == PartyQuestAsyncSaveContractStatus::Busy);
}

TEST_CASE("Timeout cancellation and artifact retirement events preserve reservation")
{
    const auto identity = Identity();
    auto next = identity;
    ++next.AttemptNonce;
    next.SaveName = SaveName(next.TransactionId, next.TargetWorldRevision, next.AttemptNonce);

    SECTION("timeout")
    {
        PartyQuestAsyncSaveContract contract;
        Begin(contract, identity);
        REQUIRE(
            contract.Poll(100 + PartyQuestAsyncSaveContract::kTimeoutMs + 1).Status ==
            PartyQuestAsyncSaveContractStatus::TimedOut);
        REQUIRE(Apply(contract, identity, Event(identity, 1, 1), 40000).Status ==
                PartyQuestNativeSaveEventAdapterStatus::TimedOut);
        REQUIRE(Apply(contract, identity, Event(identity, 1, 3), 40001).Status ==
                PartyQuestNativeSaveEventAdapterStatus::RetirementUnproven);
        REQUIRE(contract.Begin(next, 40002, false, false).Status == PartyQuestAsyncSaveContractStatus::Busy);
    }

    SECTION("cancel")
    {
        PartyQuestAsyncSaveContract contract;
        Begin(contract, identity);
        REQUIRE(contract.Cancel(identity).Status == PartyQuestAsyncSaveContractStatus::Cancelled);
        REQUIRE(Apply(contract, identity, Event(identity, 2, 2), 101).Status ==
                PartyQuestNativeSaveEventAdapterStatus::Cancelled);
        REQUIRE(Apply(contract, identity, Event(identity, 2, 3), 102).Status ==
                PartyQuestNativeSaveEventAdapterStatus::RetirementUnproven);
        REQUIRE(contract.Begin(next, 103, false, false).Status == PartyQuestAsyncSaveContractStatus::Busy);
    }
}

TEST_CASE("Malformed and stale packets cannot partially publish another request")
{
    PartyQuestAsyncSaveContract contract;
    const auto active = Identity();
    Begin(contract, active);

    auto malformed = Event(active, 1, 2);
    malformed[219] = 1;
    const auto rejected = Apply(contract, active, malformed, 101);
    REQUIRE(rejected.Status == PartyQuestNativeSaveEventAdapterStatus::Malformed);
    REQUIRE_FALSE(rejected.ContractResult);

    auto staleIdentity = active;
    ++staleIdentity.AttemptNonce;
    staleIdentity.SaveName = SaveName(
        staleIdentity.TransactionId,
        staleIdentity.TargetWorldRevision,
        staleIdentity.AttemptNonce);
    const auto stale = Apply(contract, active, Event(staleIdentity, 1, 2), 102);
    REQUIRE(stale.Status == PartyQuestNativeSaveEventAdapterStatus::IdentityMismatch);
    REQUIRE_FALSE(stale.ContractResult);

    const auto state = contract.Poll(103);
    REQUIRE(state.Status == PartyQuestAsyncSaveContractStatus::Pending);
    REQUIRE_FALSE(state.SkyrimEssClosed);
    REQUIRE_FALSE(state.SkyrimEssPublished);
    REQUIRE_FALSE(state.SkseCosaveClosed);
    REQUIRE_FALSE(state.SkseCosavePublished);
}

TEST_CASE("Request-wide retirement ABI v2 decoder is exact and identity owning")
{
    const auto identity = Identity();
    const auto valid = RetiredEvent(identity);

    auto decoded = PartyQuestNativeSaveEventAdapter::DecodeRequestRetired(
        valid.data(), valid.size());
    REQUIRE(decoded.Status == PartyQuestNativeSaveEventDecodeStatus::Decoded);
    REQUIRE(decoded.Event);
    REQUIRE(decoded.Event->Identity == identity);
    REQUIRE(decoded.Event->Outcome == PartyQuestNativeSaveEventOutcome::Succeeded);
    REQUIRE(decoded.Event->NativeError == 0);

    REQUIRE(
        PartyQuestNativeSaveEventAdapter::DecodeRequestRetired(nullptr, valid.size()).Status ==
        PartyQuestNativeSaveEventDecodeStatus::NullBuffer);
    REQUIRE(
        PartyQuestNativeSaveEventAdapter::DecodeRequestRetired(valid.data(), valid.size() - 1).Status ==
        PartyQuestNativeSaveEventDecodeStatus::Truncated);
    std::vector<uint8_t> extended(valid.begin(), valid.end());
    extended.push_back(0);
    REQUIRE(
        PartyQuestNativeSaveEventAdapter::DecodeRequestRetired(
            extended.data(), extended.size()).Status ==
        PartyQuestNativeSaveEventDecodeStatus::TrailingBytes);

    SECTION("declared size")
    {
        auto malformed = valid;
        WriteU32(malformed, 4, 232);
        REQUIRE(
            PartyQuestNativeSaveEventAdapter::DecodeRequestRetired(
                malformed.data(), malformed.size()).Status ==
            PartyQuestNativeSaveEventDecodeStatus::InvalidStructSize);
    }
    SECTION("outcome")
    {
        auto malformed = valid;
        malformed[216] = 3;
        REQUIRE(
            PartyQuestNativeSaveEventAdapter::DecodeRequestRetired(
                malformed.data(), malformed.size()).Status ==
            PartyQuestNativeSaveEventDecodeStatus::InvalidOutcome);
    }
    SECTION("identity reserved")
    {
        auto malformed = valid;
        malformed[215] = 1;
        REQUIRE(
            PartyQuestNativeSaveEventAdapter::DecodeRequestRetired(
                malformed.data(), malformed.size()).Status ==
            PartyQuestNativeSaveEventDecodeStatus::NonZeroReserved);
    }
    SECTION("event reserved")
    {
        auto malformed = valid;
        malformed[219] = 1;
        REQUIRE(
            PartyQuestNativeSaveEventAdapter::DecodeRequestRetired(
                malformed.data(), malformed.size()).Status ==
            PartyQuestNativeSaveEventDecodeStatus::NonZeroReserved);
    }
    SECTION("success cannot carry a native error")
    {
        auto malformed = valid;
        WriteU32(malformed, 220, 5);
        REQUIRE(
            PartyQuestNativeSaveEventAdapter::DecodeRequestRetired(
                malformed.data(), malformed.size()).Status ==
            PartyQuestNativeSaveEventDecodeStatus::InconsistentNativeError);
    }
    SECTION("failure preserves native error")
    {
        const auto failed = RetiredEvent(identity, 2, 1234);
        const auto failure = PartyQuestNativeSaveEventAdapter::DecodeRequestRetired(
            failed.data(), failed.size());
        REQUIRE(failure.Status == PartyQuestNativeSaveEventDecodeStatus::Decoded);
        REQUIRE(failure.Event);
        REQUIRE(failure.Event->Outcome == PartyQuestNativeSaveEventOutcome::Failed);
        REQUIRE(failure.Event->NativeError == 1234);
    }
    SECTION("owned copy survives callback storage reuse")
    {
        auto source = valid;
        auto owned = PartyQuestNativeSaveEventAdapter::DecodeRequestRetired(
            source.data(), source.size());
        REQUIRE(owned.Event);
        std::memset(source.data(), 0, source.size());
        REQUIRE(owned.Event->Identity == identity);
    }
}

TEST_CASE("Failed save outcome plus proven PQS4 drain releases terminal reservation")
{
    const auto identity = Identity();
    auto next = identity;
    ++next.AttemptNonce;
    next.SaveName = SaveName(next.TransactionId, next.TargetWorldRevision, next.AttemptNonce);

    PartyQuestAsyncSaveContract contract;
    Begin(contract, identity);
    REQUIRE(contract.Cancel(identity).Status == PartyQuestAsyncSaveContractStatus::Cancelled);

    const auto failed = ApplyRetired(contract, identity, RetiredEvent(identity, 2, 5));
    REQUIRE(failed.Status == PartyQuestNativeSaveEventAdapterStatus::Retired);
    REQUIRE(failed.ContractResult);
    REQUIRE(failed.ContractResult->Status == PartyQuestAsyncSaveContractStatus::Inactive);
    REQUIRE(contract.Begin(next, 101, false, false).Status == PartyQuestAsyncSaveContractStatus::Pending);
}

TEST_CASE("Request-wide retirement drain releases terminal contract ownership")
{
    const auto identity = Identity();
    auto next = identity;
    ++next.AttemptNonce;
    next.SaveName = SaveName(next.TransactionId, next.TargetWorldRevision, next.AttemptNonce);

    SECTION("request-wide retirement cannot silently discard pending work")
    {
        PartyQuestAsyncSaveContract contract;
        Begin(contract, identity);
        const auto early = ApplyRetired(contract, identity, RetiredEvent(identity, 2, 5));
        REQUIRE(early.Status == PartyQuestNativeSaveEventAdapterStatus::RetirementBeforeTerminal);
        REQUIRE(early.ContractResult);
        REQUIRE(early.ContractResult->Status == PartyQuestAsyncSaveContractStatus::Pending);
        REQUIRE(contract.Begin(next, 101, false, false).Status == PartyQuestAsyncSaveContractStatus::Busy);

        REQUIRE(contract.Cancel(identity).Status == PartyQuestAsyncSaveContractStatus::Cancelled);
        REQUIRE(ApplyRetired(contract, identity, RetiredEvent(identity)).Status ==
                PartyQuestNativeSaveEventAdapterStatus::Retired);
    }

    SECTION("timeout remains reserved until request-wide retirement")
    {
        PartyQuestAsyncSaveContract contract;
        Begin(contract, identity);
        REQUIRE(
            contract.Poll(100 + PartyQuestAsyncSaveContract::kTimeoutMs + 1).Status ==
            PartyQuestAsyncSaveContractStatus::TimedOut);
        REQUIRE(contract.Begin(next, 40000, false, false).Status == PartyQuestAsyncSaveContractStatus::Busy);
        REQUIRE(ApplyRetired(contract, identity, RetiredEvent(identity)).Status ==
                PartyQuestNativeSaveEventAdapterStatus::Retired);
        REQUIRE(contract.Begin(next, 40001, false, false).Status == PartyQuestAsyncSaveContractStatus::Pending);
    }
}

TEST_CASE("Stale or malformed request-wide retirement cannot release a newer request")
{
    PartyQuestAsyncSaveContract contract;
    const auto old = Identity();
    auto current = old;
    ++current.RuntimeGeneration;
    ++current.AttemptNonce;
    current.SaveName = SaveName(current.TransactionId, current.TargetWorldRevision, current.AttemptNonce);

    Begin(contract, old);
    REQUIRE(contract.Cancel(old).Status == PartyQuestAsyncSaveContractStatus::Cancelled);
    REQUIRE(ApplyRetired(contract, old, RetiredEvent(old)).Status ==
            PartyQuestNativeSaveEventAdapterStatus::Retired);
    REQUIRE(contract.Begin(current, 110, false, false).Status == PartyQuestAsyncSaveContractStatus::Pending);

    const auto stale = ApplyRetired(contract, current, RetiredEvent(old));
    REQUIRE(stale.Status == PartyQuestNativeSaveEventAdapterStatus::IdentityMismatch);
    REQUIRE_FALSE(stale.ContractResult);

    auto malformed = RetiredEvent(current);
    malformed[217] = 1;
    const auto rejected = ApplyRetired(contract, current, malformed);
    REQUIRE(rejected.Status == PartyQuestNativeSaveEventAdapterStatus::Malformed);
    REQUIRE_FALSE(rejected.ContractResult);

    REQUIRE(contract.Poll(111).Status == PartyQuestAsyncSaveContractStatus::Pending);
    REQUIRE(contract.Begin(old, 112, false, false).Status == PartyQuestAsyncSaveContractStatus::Busy);
}
