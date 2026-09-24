#include <catch2/catch.hpp>

#if defined(_WIN32)
#include <Games/Skyrim/PartyQuestSkyrimNativeLoadBridgeCoordinator.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace
{
using Coordinator = PartyQuestSkyrimNativeLoadBridgeCoordinator;
using CoordinatorStatus =
    PartyQuestSkyrimNativeLoadBridgeCoordinatorStatus;
using CoordinatorResult =
    PartyQuestSkyrimNativeLoadBridgeCoordinatorResult;
using Provider = PartyQuestSkyrimNativeLoadBridgeProvider;
using ProviderAbi = PartyQuestSkyrimNativeLoadBridgeProviderAbi;
using Owner = PartyQuestSkyrimNativeLoadBridgeOwner;
using OwnerStatus = PartyQuestSkyrimNativeLoadBridgeOwnerStatus;
using StateCode = PartyQuestNativeLoadBridgeOwnerResultCode;
using Phase = PartyQuestNativeLoadBridgeOwnerPhase;
using RequestPhase = PartyQuestNativeLoadBridgeOwnerRequestPhase;
using Lease = PartyQuestSkyrimNativeLoadBridgeModuleLease;
using LeaseCreateStatus =
    PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus;
using BridgeStatus = PartyQuestNativeLoadBridgeStatus;
using DescriptorResult = PartyQuestNativeLoadBridgeDescriptorResult;

constexpr uint64_t kFingerprint = 0x434F4F5244494E31ull;

Provider* g_provider{};

PartyQuestNativeLoadIdentity MakeIdentity(const char* apText) noexcept
{
    PartyQuestNativeLoadIdentity identity{};
    if (!apText)
        return identity;

    const size_t length = std::strlen(apText);
    if (length == 0u ||
        length > PartyQuestNativeLoadIdentity::kCapacity)
    {
        return identity;
    }

    identity.Length = static_cast<uint16_t>(length);
    std::memcpy(identity.Bytes, apText, length);
    return identity;
}

bool AcceptCoordinatorAddress(const uint8_t* apAddress) noexcept
{
    return apAddress != nullptr;
}

uint32_t LocalDescriptor(
    PartyQuestNativeLoadBridgeDescriptorV1* apDescriptor,
    uint32_t aDescriptorSize)
{
    if (!g_provider)
        return static_cast<uint32_t>(DescriptorResult::Unavailable);

    return ProviderAbi::GetDescriptor(
        *g_provider,
        apDescriptor,
        aDescriptorSize);
}

uint32_t LocalReserve(
    const PartyQuestNativeLoadBridgeReserveRequestV1* apRequest,
    uint32_t aRequestSize,
    PartyQuestNativeLoadBridgeReservationV1* apReservation,
    uint32_t aReservationSize)
{
    if (!g_provider)
        return static_cast<uint32_t>(BridgeStatus::BridgeUnavailable);

    return ProviderAbi::Reserve(
        *g_provider,
        apRequest,
        aRequestSize,
        apReservation,
        aReservationSize);
}

uint32_t LocalCancel(uint64_t aAttemptNonce)
{
    if (!g_provider)
        return static_cast<uint32_t>(BridgeStatus::BridgeUnavailable);

    return ProviderAbi::Cancel(*g_provider, aAttemptNonce);
}

uint32_t LocalPoll(
    uint64_t aAttemptNonce,
    PartyQuestNativeLoadBridgeCompletionV1* apCompletion,
    uint32_t aCompletionSize)
{
    if (!g_provider)
        return static_cast<uint32_t>(BridgeStatus::BridgeUnavailable);

    return ProviderAbi::Poll(
        *g_provider,
        aAttemptNonce,
        apCompletion,
        aCompletionSize);
}

uint32_t LocalRetire(uint64_t aAttemptNonce)
{
    if (!g_provider)
        return static_cast<uint32_t>(BridgeStatus::BridgeUnavailable);

    return ProviderAbi::Retire(*g_provider, aAttemptNonce);
}
} // namespace

class PartyQuestSkyrimNativeLoadBridgeCoordinatorTestAccess final
{
public:
    static Coordinator Create(
        Owner& aOwner,
        Provider& aProvider) noexcept
    {
        return Coordinator(aOwner, aProvider);
    }

    static PartyQuestSkyrimNativeLoadBridgeOwnerResult Bind(
        Owner& aOwner,
        Provider& aProvider,
        uint64_t aGeneration,
        uint64_t aFingerprint,
        LeaseCreateStatus& aLeaseStatus) noexcept
    {
        g_provider = &aProvider;
        auto lease = Lease::CreateProcessImageAuthenticated(
            aGeneration,
            aFingerprint,
            &AcceptCoordinatorAddress,
            &LocalDescriptor,
            &LocalReserve,
            &LocalCancel,
            &LocalPoll,
            &LocalRetire,
            aLeaseStatus);
        return aOwner.BindAuthenticated(std::move(lease));
    }
};

namespace
{
void BindCurrent(
    Owner& aOwner,
    Provider& aProvider)
{
    REQUIRE(aProvider.PublishReady(kFingerprint));

    auto& fence =
        PartyQuestRuntimeGenerationFence::GetProcessFence();
    REQUIRE_FALSE(fence.IsLifecycleTransitionPending());
    const uint64_t generation = fence.GetGeneration();
    REQUIRE(generation != 0u);

    LeaseCreateStatus leaseStatus{};
    const auto bound =
        PartyQuestSkyrimNativeLoadBridgeCoordinatorTestAccess::Bind(
            aOwner,
            aProvider,
            generation,
            kFingerprint,
            leaseStatus);

    REQUIRE(leaseStatus == LeaseCreateStatus::Ready);
    REQUIRE(bound.Status == OwnerStatus::Applied);
    REQUIRE(bound.State.Code == StateCode::Bound);
    REQUIRE(aOwner.Snapshot().Phase == Phase::Bound);
    REQUIRE(aOwner.Snapshot().BoundGeneration == generation);
}

void CompleteTransition(
    PartyQuestRuntimeGenerationFence::LifecycleTransitionTicket aTicket)
{
    REQUIRE(aTicket.IsValid());
    REQUIRE(
        PartyQuestRuntimeGenerationFence::GetProcessFence().
            CompleteLifecycleTransition(aTicket));
}
} // namespace

TEST_CASE(
    "Native load process coordinator POD surfaces remain deterministic",
    "[quest.party-state][native-load-coordinator][abi]")
{
    STATIC_REQUIRE(sizeof(
        PartyQuestSkyrimNativeLoadBridgeCoordinatorStatus) == 1u);
    STATIC_REQUIRE(std::is_standard_layout_v<
        PartyQuestSkyrimNativeLoadBridgeAttempt>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<
        PartyQuestSkyrimNativeLoadBridgeAttempt>);
    STATIC_REQUIRE(std::is_standard_layout_v<
        PartyQuestSkyrimNativeLoadBridgeCoordinatorResult>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<
        PartyQuestSkyrimNativeLoadBridgeCoordinatorResult>);
}

TEST_CASE(
    "Native load process coordinator bypasses only completely inactive bridge",
    "[quest.party-state][native-load-coordinator][inactive]")
{
    Provider provider;
    Owner owner;
    auto coordinator =
        PartyQuestSkyrimNativeLoadBridgeCoordinatorTestAccess::Create(
            owner,
            provider);

    const auto bypassed =
        coordinator.Begin(MakeIdentity("InactiveSave_TEST"));
    REQUIRE(bypassed.Status == CoordinatorStatus::Bypassed);
    REQUIRE(bypassed.IsBypassed());
    REQUIRE_FALSE(bypassed.Attempt.IsValid());
    REQUIRE(owner.Snapshot().Phase == Phase::Unbound);
    REQUIRE(provider.GetState() ==
        PartyQuestNativeLoadBridgeAdapterState::NotReady);

    REQUIRE(provider.PublishReady(kFingerprint));
    const auto rejected =
        coordinator.Begin(MakeIdentity("EnabledButUnbound_TEST"));
    REQUIRE(rejected.Status == CoordinatorStatus::Rejected);
    REQUIRE_FALSE(rejected.IsBypassed());
    REQUIRE_FALSE(rejected.Attempt.IsValid());
}

TEST_CASE(
    "Native load process coordinator completes false-result target before TES lifecycle ticket",
    "[quest.party-state][native-load-coordinator][generation][false-result]")
{
    Provider provider;
    Owner owner;
    BindCurrent(owner, provider);
    auto coordinator =
        PartyQuestSkyrimNativeLoadBridgeCoordinatorTestAccess::Create(
            owner,
            provider);

    const auto identity =
        MakeIdentity("Save42_AFFE5420_TEST");
    const auto begun = coordinator.Begin(identity);
    REQUIRE(begun.Status == CoordinatorStatus::Reserved);
    REQUIRE(begun.Attempt.IsValid());
    REQUIRE(begun.Owner.State.Code == StateCode::Reserved);
    REQUIRE(provider.GetActiveAttemptNonce() ==
        begun.Attempt.AttemptNonce);

    auto& fence =
        PartyQuestRuntimeGenerationFence::GetProcessFence();

    // Mirrors PartyQuestRuntimeSessionOwner::PrepareAndRelease(LoadGame).
    auto invalidation = fence.TryBeginInvalidation();
    REQUIRE(invalidation);
    REQUIRE(invalidation->IsValid());
    const uint64_t firstGeneration =
        invalidation->GetGeneration();
    REQUIRE(firstGeneration > begun.Attempt.ReservedGeneration);

    const auto firstObserved =
        coordinator.ObserveGeneration(
            begun.Attempt,
            firstGeneration);
    REQUIRE(
        firstObserved.Status ==
        CoordinatorStatus::GenerationObserved);
    REQUIRE(firstObserved.Owner.State.Code ==
        StateCode::DrainPending);
    REQUIRE(owner.Snapshot().Phase == Phase::DrainOnly);
    invalidation.reset();

    // Mirrors the second async lifecycle ticket in SaveLoad.cpp.
    const auto ticket = fence.BeginLifecycleTransition();
    REQUIRE(ticket.IsValid());
    REQUIRE(ticket.Generation > firstGeneration);

    const auto secondObserved =
        coordinator.ObserveGeneration(
            begun.Attempt,
            ticket.Generation);
    REQUIRE(
        secondObserved.Status ==
        CoordinatorStatus::GenerationObserved);
    REQUIRE(owner.Snapshot().CurrentGeneration ==
        ticket.Generation);
    REQUIRE(owner.Snapshot().BoundGeneration ==
        begun.Attempt.ReservedGeneration);
    REQUIRE(fence.IsLifecycleTransitionPending());

    const auto entered =
        coordinator.EnterTarget(begun.Attempt, identity);
    REQUIRE(entered.Status == CoordinatorStatus::TargetEntered);
    REQUIRE(entered.ProviderStatus == BridgeStatus::Pending);

    const auto completed =
        coordinator.CompleteTarget(begun.Attempt, false);
    REQUIRE(completed.Status == CoordinatorStatus::Completed);
    REQUIRE(completed.IsCompleted());
    REQUIRE(completed.ProviderStatus == BridgeStatus::Retired);
    REQUIRE(completed.HasCompletion == 1u);
    REQUIRE(completed.Completion.AttemptNonce ==
        begun.Attempt.AttemptNonce);
    REQUIRE(completed.Completion.Result == 0u);
    REQUIRE(completed.Completion.EventSequence != 0u);

    // Native target completion/retirement is intentionally earlier than and
    // independent from TESLoadGameEvent lifecycle completion.
    REQUIRE(fence.IsLifecycleTransitionPending());
    REQUIRE(owner.Snapshot().Phase == Phase::Unbound);
    REQUIRE(owner.Snapshot().RequestPhase == RequestPhase::None);
    REQUIRE(owner.Snapshot().CapabilityRetained == 0u);
    REQUIRE(owner.Snapshot().CurrentGeneration ==
        ticket.Generation);
    REQUIRE(provider.GetActiveAttemptNonce() == 0u);

    CompleteTransition(ticket);
}

TEST_CASE(
    "Native load process coordinator cancels exact reservation on pre-target identity mismatch",
    "[quest.party-state][native-load-coordinator][identity][cancel]")
{
    Provider provider;
    Owner owner;
    BindCurrent(owner, provider);
    auto coordinator =
        PartyQuestSkyrimNativeLoadBridgeCoordinatorTestAccess::Create(
            owner,
            provider);

    const auto expected =
        MakeIdentity("ExpectedSave_TEST");
    const auto begun = coordinator.Begin(expected);
    REQUIRE(begun.Status == CoordinatorStatus::Reserved);

    auto& fence =
        PartyQuestRuntimeGenerationFence::GetProcessFence();
    auto invalidation = fence.TryBeginInvalidation();
    REQUIRE(invalidation);
    const uint64_t generation =
        invalidation->GetGeneration();
    REQUIRE(
        coordinator.ObserveGeneration(
            begun.Attempt,
            generation).Status ==
        CoordinatorStatus::GenerationObserved);
    invalidation.reset();

    const auto rejected =
        coordinator.EnterTarget(
            begun.Attempt,
            MakeIdentity("DifferentSave_TEST"));

    REQUIRE(rejected.Status == CoordinatorStatus::Cancelled);
    REQUIRE(rejected.ProviderStatus == BridgeStatus::Cancelled);
    REQUIRE(rejected.Owner.State.Code == StateCode::Cancelled);
    REQUIRE(rejected.Owner.State.ReleaseCapability == 1u);
    REQUIRE(owner.Snapshot().Phase == Phase::Unbound);
    REQUIRE(owner.Snapshot().RequestPhase == RequestPhase::None);
    REQUIRE(owner.Snapshot().CapabilityRetained == 0u);
    REQUIRE(provider.GetActiveAttemptNonce() == 0u);
}

TEST_CASE(
    "Native load process coordinator can cancel reserved request before target entry",
    "[quest.party-state][native-load-coordinator][cancel]")
{
    Provider provider;
    Owner owner;
    BindCurrent(owner, provider);
    auto coordinator =
        PartyQuestSkyrimNativeLoadBridgeCoordinatorTestAccess::Create(
            owner,
            provider);

    const auto begun =
        coordinator.Begin(MakeIdentity("CancelledSave_TEST"));
    REQUIRE(begun.Status == CoordinatorStatus::Reserved);

    const auto cancelled =
        coordinator.CancelBeforeTarget(begun.Attempt);
    REQUIRE(cancelled.Status == CoordinatorStatus::Cancelled);
    REQUIRE(cancelled.Owner.State.Code == StateCode::Cancelled);
    REQUIRE(cancelled.Owner.State.ReleaseCapability == 0u);
    REQUIRE(owner.Snapshot().Phase == Phase::Bound);
    REQUIRE(owner.Snapshot().RequestPhase == RequestPhase::None);
    REQUIRE(owner.Snapshot().CapabilityRetained == 1u);
    REQUIRE(provider.GetActiveAttemptNonce() == 0u);
}

TEST_CASE(
    "Native load process coordinator rejects stale attempt without touching newer reservation",
    "[quest.party-state][native-load-coordinator][stale]")
{
    Provider provider;
    Owner owner;
    BindCurrent(owner, provider);
    auto coordinator =
        PartyQuestSkyrimNativeLoadBridgeCoordinatorTestAccess::Create(
            owner,
            provider);

    const auto first =
        coordinator.Begin(MakeIdentity("FirstSave_TEST"));
    REQUIRE(first.Status == CoordinatorStatus::Reserved);
    REQUIRE(
        coordinator.CancelBeforeTarget(first.Attempt).Status ==
        CoordinatorStatus::Cancelled);

    const auto second =
        coordinator.Begin(MakeIdentity("SecondSave_TEST"));
    REQUIRE(second.Status == CoordinatorStatus::Reserved);
    REQUIRE(second.Attempt.AttemptNonce >
        first.Attempt.AttemptNonce);

    const auto stale =
        coordinator.EnterTarget(
            first.Attempt,
            MakeIdentity("FirstSave_TEST"));
    REQUIRE(stale.Status == CoordinatorStatus::Rejected);
    REQUIRE(provider.GetActiveAttemptNonce() ==
        second.Attempt.AttemptNonce);
    REQUIRE(owner.Snapshot().ActiveAttemptNonce ==
        second.Attempt.AttemptNonce);

    REQUIRE(
        coordinator.CancelBeforeTarget(second.Attempt).Status ==
        CoordinatorStatus::Cancelled);
}
#endif
