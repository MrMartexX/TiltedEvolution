#include <Games/Skyrim/PartyQuestSkyrimNativeLoadBridgeOwner.h>

#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>

#include <catch2/catch.hpp>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <utility>

#if defined(_WIN32)
#include <Windows.h>

namespace
{
using Owner = PartyQuestSkyrimNativeLoadBridgeOwner;
using OwnerStatus = PartyQuestSkyrimNativeLoadBridgeOwnerStatus;
using OwnerResult = PartyQuestSkyrimNativeLoadBridgeOwnerResult;
using ShutdownDrainStatus =
    PartyQuestSkyrimNativeLoadBridgeShutdownDrainStatus;
using Lease = PartyQuestSkyrimNativeLoadBridgeModuleLease;
using LeaseCreateStatus =
    PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus;
using StateCode = PartyQuestNativeLoadBridgeOwnerResultCode;
using Phase = PartyQuestNativeLoadBridgeOwnerPhase;
using RequestPhase = PartyQuestNativeLoadBridgeOwnerRequestPhase;
using Status = PartyQuestNativeLoadBridgeStatus;
using Identity = PartyQuestNativeLoadBridgeIdentityV1;
using ForeignDisposition =
    PartyQuestNativeLoadBridgeOwnerForeignDisposition;

constexpr uint64_t kFingerprint = 0x4F574E45524C3031ull;

struct FakeState final
{
    uint32_t ReserveCalls{};
    uint32_t CancelCalls{};
    uint32_t PollCalls{};
    uint32_t RetireCalls{};

    bool ReserveSawGenerationLease{};
    bool CancelSawGenerationLease{};
    bool PollSawGenerationLease{};
    bool RetireSawGenerationLease{};

    uint64_t NextNonce{101u};
    uint64_t CompletionSequence{201u};

    Status CancelStatus{Status::Cancelled};
    Status PollStatus{Status::Pending};
    bool CancelRaiseSeh{};
    Status RetireStatus{Status::Retired};
    uint8_t CompletionResult{1u};

    Identity ReservedIdentity;

    Owner* ReentrantOwner{};
    Owner* ReentrantCancelDrainOwner{};
    uint64_t ReentrantGeneration{};
    OwnerStatus ReentrantShutdownStatus{
        OwnerStatus::SynchronizationFailed};
    OwnerStatus ReentrantObserveStatus{
        OwnerStatus::SynchronizationFailed};
    ShutdownDrainStatus ReentrantCancelDrainStatus{
        ShutdownDrainStatus::UnexpectedOwnerState};
};

FakeState g_fake;

void ResetFake() noexcept
{
    g_fake = {};
    g_fake.NextNonce = 101u;
    g_fake.CompletionSequence = 201u;
    g_fake.CancelStatus = Status::Cancelled;
    g_fake.PollStatus = Status::Pending;
    g_fake.RetireStatus = Status::Retired;
    g_fake.CompletionResult = 1u;
    g_fake.ReentrantShutdownStatus =
        OwnerStatus::SynchronizationFailed;
    g_fake.ReentrantObserveStatus =
        OwnerStatus::SynchronizationFailed;
}

Identity MakeIdentity(const char* apText) noexcept
{
    Identity identity{};
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

uint32_t FakeGetDescriptor(
    PartyQuestNativeLoadBridgeDescriptorV1* apDescriptor,
    uint32_t aSize)
{
    if (!apDescriptor ||
        aSize != sizeof(PartyQuestNativeLoadBridgeDescriptorV1))
    {
        return static_cast<uint32_t>(
            PartyQuestNativeLoadBridgeDescriptorResult::Unavailable);
    }

    *apDescriptor = {};
    return static_cast<uint32_t>(
        PartyQuestNativeLoadBridgeDescriptorResult::Available);
}

uint32_t FakeReserve(
    const PartyQuestNativeLoadBridgeReserveRequestV1* apRequest,
    uint32_t aRequestSize,
    PartyQuestNativeLoadBridgeReservationV1* apReservation,
    uint32_t aReservationSize)
{
    ++g_fake.ReserveCalls;
    g_fake.ReserveSawGenerationLease =
        PartyQuestRuntimeGenerationFence::GetProcessFence().
            IsExecutionLeaseHeldByCurrentThread();

    if (g_fake.ReentrantOwner)
    {
        if (g_fake.ReentrantGeneration != 0u)
        {
            g_fake.ReentrantObserveStatus =
                g_fake.ReentrantOwner->ObserveGeneration(
                    g_fake.ReentrantGeneration).Status;
        }
        g_fake.ReentrantShutdownStatus =
            g_fake.ReentrantOwner->Shutdown().Status;
    }

    if (!apRequest ||
        !apReservation ||
        aRequestSize != sizeof(*apRequest) ||
        aReservationSize != sizeof(*apReservation))
    {
        return static_cast<uint32_t>(Status::InvalidArgument);
    }

    *apReservation = {};
    apReservation->AbiVersion =
        kPartyQuestNativeLoadBridgePayloadAbi;
    apReservation->StructSize = sizeof(*apReservation);
    apReservation->AttemptNonce = g_fake.NextNonce;
    apReservation->Identity = apRequest->Identity;
    g_fake.ReservedIdentity = apRequest->Identity;
    return static_cast<uint32_t>(Status::Reserved);
}

uint32_t FakeCancel(uint64_t)
{
    ++g_fake.CancelCalls;
    if (g_fake.ReentrantCancelDrainOwner)
    {
        g_fake.ReentrantCancelDrainStatus =
            g_fake.ReentrantCancelDrainOwner->DrainShutdown(0u).Status;
    }

    if (g_fake.CancelRaiseSeh)
    {
        ::RaiseException(0xE0425154u, 0u, 0u, nullptr);
        return static_cast<uint32_t>(Status::InternalFailure);
    }
    g_fake.CancelSawGenerationLease =
        PartyQuestRuntimeGenerationFence::GetProcessFence().
            IsExecutionLeaseHeldByCurrentThread();
    return static_cast<uint32_t>(g_fake.CancelStatus);
}

uint32_t FakePoll(
    uint64_t aAttemptNonce,
    PartyQuestNativeLoadBridgeCompletionV1* apCompletion,
    uint32_t aCompletionSize)
{
    ++g_fake.PollCalls;
    g_fake.PollSawGenerationLease =
        PartyQuestRuntimeGenerationFence::GetProcessFence().
            IsExecutionLeaseHeldByCurrentThread();

    if (!apCompletion ||
        aCompletionSize != sizeof(*apCompletion))
    {
        return static_cast<uint32_t>(Status::InvalidArgument);
    }

    *apCompletion = {};
    if (g_fake.PollStatus == Status::CompletionAvailable)
    {
        apCompletion->AbiVersion =
            kPartyQuestNativeLoadBridgePayloadAbi;
        apCompletion->StructSize = sizeof(*apCompletion);
        apCompletion->AttemptNonce = aAttemptNonce;
        apCompletion->EventSequence = g_fake.CompletionSequence;
        apCompletion->Identity = g_fake.ReservedIdentity;
        apCompletion->Result = g_fake.CompletionResult;
    }

    return static_cast<uint32_t>(g_fake.PollStatus);
}

uint32_t FakeRetire(uint64_t)
{
    ++g_fake.RetireCalls;
    g_fake.RetireSawGenerationLease =
        PartyQuestRuntimeGenerationFence::GetProcessFence().
            IsExecutionLeaseHeldByCurrentThread();
    return static_cast<uint32_t>(g_fake.RetireStatus);
}

template <class T>
void* ModuleFor(T apFunction) noexcept
{
    HMODULE module = nullptr;
    if (!apFunction ||
        !::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(apFunction),
            &module))
    {
        return nullptr;
    }

    return module;
}

} // namespace

class PartyQuestSkyrimNativeLoadBridgeOwnerTestAccess final
{
public:
    static Lease CreateLease(
        uint64_t aGeneration,
        LeaseCreateStatus& aStatus) noexcept
    {
        return Lease::CreateAuthenticated(
            ModuleFor(&FakeGetDescriptor),
            aGeneration,
            kFingerprint,
            &FakeGetDescriptor,
            &FakeReserve,
            &FakeCancel,
            &FakePoll,
            &FakeRetire,
            aStatus);
    }

    static OwnerResult Bind(
        Owner& aOwner,
        Lease&& aLease) noexcept
    {
        return aOwner.BindAuthenticated(std::move(aLease));
    }
};

namespace
{
OwnerResult BindCurrent(Owner& aOwner)
{
    const uint64_t generation =
        PartyQuestRuntimeGenerationFence::GetProcessFence().GetGeneration();
    REQUIRE(generation != 0u);

    LeaseCreateStatus createStatus{};
    auto lease =
        PartyQuestSkyrimNativeLoadBridgeOwnerTestAccess::CreateLease(
            generation,
            createStatus);
    REQUIRE(createStatus == LeaseCreateStatus::Ready);
    REQUIRE(lease.IsCallable());

    const auto bound =
        PartyQuestSkyrimNativeLoadBridgeOwnerTestAccess::Bind(
            aOwner,
            std::move(lease));
    REQUIRE(bound.Status == OwnerStatus::Applied);
    REQUIRE(bound.State.Code == StateCode::Bound);
    REQUIRE(bound.ForeignCallAttempted == 0u);
    return bound;
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
#endif

#if defined(_WIN32)
TEST_CASE(
    "Native load bridge process owner has one stable process lifetime identity",
    "[quest.party-state][native-load-owner-core][process-owner]")
{
    auto& first =
        PartyQuestSkyrimNativeLoadBridgeOwner::GetProcessOwner();
    auto& second =
        PartyQuestSkyrimNativeLoadBridgeOwner::GetProcessOwner();

    REQUIRE(&first == &second);
}

#endif

TEST_CASE(
    "Native load bridge owner is one non-movable serialization domain",
    "[quest.party-state][native-load-owner-core][abi]")
{
    using Owner = PartyQuestSkyrimNativeLoadBridgeOwner;
    using Status = PartyQuestSkyrimNativeLoadBridgeOwnerStatus;
    using Result = PartyQuestSkyrimNativeLoadBridgeOwnerResult;
    using DrainStatus =
        PartyQuestSkyrimNativeLoadBridgeShutdownDrainStatus;
    using DrainResult =
        PartyQuestSkyrimNativeLoadBridgeShutdownDrainResult;

    STATIC_REQUIRE(sizeof(Status) == 1u);
    STATIC_REQUIRE(sizeof(DrainStatus) == 1u);
    STATIC_REQUIRE(std::is_standard_layout_v<Result>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Result>);
    STATIC_REQUIRE(std::is_standard_layout_v<DrainResult>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<DrainResult>);
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<Owner>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<Owner>);
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<Owner>);
    STATIC_REQUIRE_FALSE(std::is_move_assignable_v<Owner>);
}

#if defined(_WIN32)
TEST_CASE(
    "Native load owner holds current generation lease across Reserve foreign call",
    "[quest.party-state][native-load-owner-core][generation][reserve]")
{
    ResetFake();
    Owner owner;
    BindCurrent(owner);

    const auto reserved =
        owner.Reserve(MakeIdentity("ManualSave41.ess"));
    REQUIRE(reserved.Status == OwnerStatus::Applied);
    REQUIRE(reserved.State.Code == StateCode::Reserved);
    REQUIRE(reserved.ForeignCallAttempted == 1u);
    REQUIRE(g_fake.ReserveCalls == 1u);
    REQUIRE(g_fake.ReserveSawGenerationLease);

    const auto snapshot = owner.Snapshot();
    REQUIRE(snapshot.Phase == Phase::Bound);
    REQUIRE(snapshot.RequestPhase == RequestPhase::Active);
    REQUIRE(snapshot.ActiveAttemptNonce == g_fake.NextNonce);
    REQUIRE_FALSE(owner.IsOperationActive());

    const auto cancelled = owner.Cancel(snapshot.ActiveAttemptNonce);
    REQUIRE(cancelled.Status == OwnerStatus::Applied);
    REQUIRE(cancelled.State.Code == StateCode::Cancelled);
    REQUIRE(cancelled.ForeignCallAttempted == 1u);
    REQUIRE(g_fake.CancelCalls == 1u);
    REQUIRE(g_fake.CancelSawGenerationLease);
    REQUIRE(owner.Snapshot().Phase == Phase::Bound);
}

TEST_CASE(
    "Owner observes synchronous generation while caller still holds exclusive invalidation lease",
    "[quest.party-state][native-load-owner-core][generation][exclusive]")
{
    ResetFake();
    Owner owner;
    BindCurrent(owner);

    const auto reserved =
        owner.Reserve(MakeIdentity("ManualSave47.ess"));
    REQUIRE(reserved.State.Code == StateCode::Reserved);
    const uint64_t nonce = owner.Snapshot().ActiveAttemptNonce;

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    auto invalidation = fence.TryBeginInvalidation();
    REQUIRE(invalidation);
    REQUIRE(invalidation->IsValid());
    const uint64_t generation = invalidation->GetGeneration();

    // This mirrors PartyQuestRuntimeSessionOwner::PrepareAndRelease: lifecycle
    // observation runs before the exclusive invalidation lease is destroyed.
    const auto observed = owner.ObserveGeneration(generation);
    REQUIRE(observed.Status == OwnerStatus::Applied);
    REQUIRE(observed.State.Code == StateCode::DrainPending);
    REQUIRE(observed.State.ReleaseCapability == 0u);
    REQUIRE(owner.Snapshot().Phase == Phase::DrainOnly);
    REQUIRE(owner.Snapshot().CurrentGeneration == generation);

    invalidation.reset();

    g_fake.CancelStatus = Status::Cancelled;
    const auto drained = owner.Cancel(nonce);
    REQUIRE(drained.State.Code == StateCode::Cancelled);
    REQUIRE(drained.State.ReleaseCapability == 1u);
    REQUIRE_FALSE(g_fake.CancelSawGenerationLease);
    REQUIRE(owner.Snapshot().Phase == Phase::Unbound);
}

TEST_CASE(
    "Generation advance blocks old Bound calls until owner reconciles",
    "[quest.party-state][native-load-owner-core][generation][reconcile]")
{
    ResetFake();
    Owner owner;
    BindCurrent(owner);

    const auto reserved =
        owner.Reserve(MakeIdentity("ManualSave42.ess"));
    REQUIRE(reserved.State.Code == StateCode::Reserved);
    const uint64_t nonce = owner.Snapshot().ActiveAttemptNonce;

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const auto ticket = fence.BeginLifecycleTransition();
    REQUIRE(ticket.IsValid());

    const uint32_t pollsBefore = g_fake.PollCalls;
    const auto blocked = owner.Poll(nonce);
    REQUIRE(blocked.Status == OwnerStatus::GenerationUnavailable);
    REQUIRE(blocked.ForeignCallAttempted == 0u);
    REQUIRE(g_fake.PollCalls == pollsBefore);
    REQUIRE(owner.Snapshot().Phase == Phase::Bound);
    REQUIRE(owner.Snapshot().PendingEffect ==
        PartyQuestNativeLoadBridgeOwnerEffectKind::None);

    const auto observed = owner.ObserveGeneration(ticket.Generation);
    REQUIRE(observed.Status == OwnerStatus::Applied);
    REQUIRE(observed.State.Code == StateCode::DrainPending);
    REQUIRE(observed.State.ReleaseCapability == 0u);
    REQUIRE(owner.Snapshot().Phase == Phase::DrainOnly);

    g_fake.CancelStatus = Status::Cancelled;
    const auto drained = owner.Cancel(nonce);
    REQUIRE(drained.State.Code == StateCode::Cancelled);
    REQUIRE(drained.State.ReleaseCapability == 1u);
    REQUIRE_FALSE(g_fake.CancelSawGenerationLease);
    REQUIRE(owner.Snapshot().Phase == Phase::Unbound);

    CompleteTransition(ticket);
}

TEST_CASE(
    "Old request drains during pending generation transition without old execution lease",
    "[quest.party-state][native-load-owner-core][generation][drain]")
{
    ResetFake();
    Owner owner;
    BindCurrent(owner);

    const auto reserved =
        owner.Reserve(MakeIdentity("ManualSave43.ess"));
    REQUIRE(reserved.State.Code == StateCode::Reserved);
    const uint64_t nonce = owner.Snapshot().ActiveAttemptNonce;

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const auto ticket = fence.BeginLifecycleTransition();
    REQUIRE(ticket.IsValid());

    REQUIRE(owner.ObserveGeneration(ticket.Generation).State.Code ==
        StateCode::DrainPending);
    REQUIRE(owner.Snapshot().Phase == Phase::DrainOnly);

    g_fake.CancelStatus = Status::InvalidState;
    const auto cancel = owner.Cancel(nonce);
    REQUIRE(cancel.Status == OwnerStatus::Applied);
    REQUIRE(cancel.State.Code == StateCode::Pending);
    REQUIRE(cancel.State.ReleaseCapability == 0u);
    REQUIRE(g_fake.CancelCalls == 1u);
    REQUIRE_FALSE(g_fake.CancelSawGenerationLease);

    g_fake.PollStatus = Status::Pending;
    const auto pending = owner.Poll(nonce);
    REQUIRE(pending.State.Code == StateCode::Pending);
    REQUIRE(pending.State.ReleaseCapability == 0u);
    REQUIRE_FALSE(g_fake.PollSawGenerationLease);

    g_fake.PollStatus = Status::CompletionAvailable;
    g_fake.CompletionResult = 0u;
    const auto completed = owner.Poll(nonce);
    REQUIRE(completed.State.Code == StateCode::CompletionAvailable);
    REQUIRE(completed.State.HasCompletion == 1u);
    REQUIRE(completed.State.Completion.Result == 0u);
    REQUIRE(completed.State.ReleaseCapability == 0u);
    REQUIRE_FALSE(g_fake.PollSawGenerationLease);

    const auto retired = owner.Retire(nonce);
    REQUIRE(retired.State.Code == StateCode::Retired);
    REQUIRE(retired.State.ReleaseCapability == 1u);
    REQUIRE(g_fake.RetireCalls == 1u);
    REQUIRE_FALSE(g_fake.RetireSawGenerationLease);
    REQUIRE(owner.Snapshot().Phase == Phase::Unbound);
    REQUIRE(owner.Snapshot().CurrentGeneration == ticket.Generation);

    CompleteTransition(ticket);

    LeaseCreateStatus createStatus{};
    auto newLease =
        PartyQuestSkyrimNativeLoadBridgeOwnerTestAccess::CreateLease(
            ticket.Generation,
            createStatus);
    REQUIRE(createStatus == LeaseCreateStatus::Ready);
    const auto rebound =
        PartyQuestSkyrimNativeLoadBridgeOwnerTestAccess::Bind(
            owner,
            std::move(newLease));
    REQUIRE(rebound.Status == OwnerStatus::Applied);
    REQUIRE(rebound.State.Code == StateCode::Bound);
    REQUIRE(owner.Snapshot().BoundGeneration == ticket.Generation);
}

TEST_CASE(
    "Shutdown claimed request retains capability until completion and exact Retire",
    "[quest.party-state][native-load-owner-core][shutdown][drain]")
{
    ResetFake();
    Owner owner;
    BindCurrent(owner);

    const auto reserved =
        owner.Reserve(MakeIdentity("ManualSave44.ess"));
    REQUIRE(reserved.State.Code == StateCode::Reserved);
    const uint64_t nonce = owner.Snapshot().ActiveAttemptNonce;

    g_fake.CancelStatus = Status::InvalidState;
    const auto firstShutdown = owner.Shutdown();
    REQUIRE(firstShutdown.Status == OwnerStatus::Applied);
    REQUIRE(firstShutdown.State.Code == StateCode::Pending);
    REQUIRE(firstShutdown.State.ReleaseCapability == 0u);
    REQUIRE(firstShutdown.ForeignCallAttempted == 1u);
    REQUIRE(owner.IsShutdownRequested());
    REQUIRE(owner.Snapshot().Phase == Phase::ShutdownDrain);
    REQUIRE(owner.Snapshot().CapabilityRetained == 1u);
    REQUIRE_FALSE(g_fake.CancelSawGenerationLease);

    REQUIRE(owner.Reserve(MakeIdentity("blocked.ess")).Status ==
        OwnerStatus::AdmissionClosed);

    g_fake.PollStatus = Status::Pending;
    REQUIRE(owner.Poll(nonce).State.Code == StateCode::Pending);
    REQUIRE_FALSE(g_fake.PollSawGenerationLease);

    g_fake.PollStatus = Status::CompletionAvailable;
    const auto completed = owner.Poll(nonce);
    REQUIRE(completed.State.Code == StateCode::CompletionAvailable);
    REQUIRE(completed.State.ReleaseCapability == 0u);

    const auto finalShutdown = owner.Shutdown();
    REQUIRE(finalShutdown.Status == OwnerStatus::Applied);
    REQUIRE(finalShutdown.State.Code == StateCode::Retired);
    REQUIRE(finalShutdown.State.ReleaseCapability == 1u);
    REQUIRE(finalShutdown.ForeignCallAttempted == 1u);
    REQUIRE(g_fake.RetireCalls == 1u);
    REQUIRE_FALSE(g_fake.RetireSawGenerationLease);

    const auto snapshot = owner.Snapshot();
    REQUIRE(snapshot.Phase == Phase::ShutdownComplete);
    REQUIRE(snapshot.CapabilityRetained == 0u);

    const auto duplicateShutdown = owner.Shutdown();
    REQUIRE(duplicateShutdown.State.Code == StateCode::ShutdownComplete);
    REQUIRE(duplicateShutdown.State.ReleaseCapability == 0u);
    REQUIRE(duplicateShutdown.ForeignCallAttempted == 0u);
}

TEST_CASE(
    "Shutdown drain driver completes immediate cancel and releases capability exactly once",
    "[quest.party-state][native-load-owner-core][shutdown][driver][cancel]")
{
    ResetFake();
    Owner owner;
    BindCurrent(owner);

    REQUIRE(
        owner.Reserve(MakeIdentity("DrainImmediate.ess")).State.Code ==
        StateCode::Reserved);

    g_fake.CancelStatus = Status::Cancelled;
    const auto drained = owner.DrainShutdown(0u);

    REQUIRE(drained.Status == ShutdownDrainStatus::Completed);
    REQUIRE(drained.IsSafeToTeardown());
    REQUIRE_FALSE(drained.MustRetainCapability());
    REQUIRE(drained.PollCalls == 0u);
    REQUIRE(drained.KnownPendingPolls == 0u);
    REQUIRE(drained.FinalPhase == Phase::ShutdownComplete);
    REQUIRE(drained.FinalRequestPhase == RequestPhase::None);
    REQUIRE(g_fake.CancelCalls == 1u);
    REQUIRE(g_fake.PollCalls == 0u);
    REQUIRE(g_fake.RetireCalls == 0u);

    const auto duplicate = owner.DrainShutdown(8u);
    REQUIRE(duplicate.Status == ShutdownDrainStatus::Completed);
    REQUIRE(duplicate.IsSafeToTeardown());
    REQUIRE(g_fake.CancelCalls == 1u);
    REQUIRE(g_fake.PollCalls == 0u);
    REQUIRE(g_fake.RetireCalls == 0u);
}

TEST_CASE(
    "Shutdown drain zero poll budget closes admission but retains claimed request",
    "[quest.party-state][native-load-owner-core][shutdown][driver][budget-zero]")
{
    ResetFake();
    Owner owner;
    BindCurrent(owner);

    REQUIRE(
        owner.Reserve(MakeIdentity("DrainZero.ess")).State.Code ==
        StateCode::Reserved);

    g_fake.CancelStatus = Status::InvalidState;
    const auto exhausted = owner.DrainShutdown(0u);

    REQUIRE(
        exhausted.Status ==
        ShutdownDrainStatus::KnownPendingBudgetExhausted);
    REQUIRE_FALSE(exhausted.IsSafeToTeardown());
    REQUIRE(exhausted.MustRetainCapability());
    REQUIRE(exhausted.PollCalls == 0u);
    REQUIRE(exhausted.KnownPendingPolls == 0u);
    REQUIRE(exhausted.FinalPhase == Phase::ShutdownDrain);
    REQUIRE(exhausted.FinalRequestPhase == RequestPhase::Active);
    REQUIRE(g_fake.CancelCalls == 1u);
    REQUIRE(g_fake.PollCalls == 0u);
    REQUIRE(owner.IsShutdownRequested());
    REQUIRE(owner.Reserve(MakeIdentity("blocked.ess")).Status ==
        OwnerStatus::AdmissionClosed);
}

TEST_CASE(
    "Shutdown drain known Pending budget exhausts without poisoning or releasing capability",
    "[quest.party-state][native-load-owner-core][shutdown][driver][budget]")
{
    ResetFake();
    Owner owner;
    BindCurrent(owner);

    REQUIRE(
        owner.Reserve(MakeIdentity("DrainPending.ess")).State.Code ==
        StateCode::Reserved);

    g_fake.CancelStatus = Status::InvalidState;
    g_fake.PollStatus = Status::Pending;

    const auto exhausted = owner.DrainShutdown(3u);
    REQUIRE(
        exhausted.Status ==
        ShutdownDrainStatus::KnownPendingBudgetExhausted);
    REQUIRE_FALSE(exhausted.IsSafeToTeardown());
    REQUIRE(exhausted.MustRetainCapability());
    REQUIRE(exhausted.PollCalls == 3u);
    REQUIRE(exhausted.KnownPendingPolls == 3u);
    REQUIRE(exhausted.FinalPhase == Phase::ShutdownDrain);
    REQUIRE(exhausted.FinalRequestPhase == RequestPhase::Active);
    REQUIRE(g_fake.CancelCalls == 1u);
    REQUIRE(g_fake.PollCalls == 3u);
    REQUIRE(g_fake.RetireCalls == 0u);

    // A later process policy may continue the same exact retained request.
    g_fake.PollStatus = Status::CompletionAvailable;
    g_fake.CompletionResult = 0u;
    const auto resumed = owner.DrainShutdown(1u);
    REQUIRE(resumed.Status == ShutdownDrainStatus::Completed);
    REQUIRE(resumed.IsSafeToTeardown());
    REQUIRE_FALSE(resumed.MustRetainCapability());
    REQUIRE(resumed.PollCalls == 1u);
    REQUIRE(resumed.KnownPendingPolls == 0u);
    REQUIRE(resumed.Last.State.Code == StateCode::Retired);
    REQUIRE(g_fake.CancelCalls == 1u);
    REQUIRE(g_fake.PollCalls == 4u);
    REQUIRE(g_fake.RetireCalls == 1u);
}

TEST_CASE(
    "Shutdown drain completion retires exact request without old generation lease",
    "[quest.party-state][native-load-owner-core][shutdown][driver][retire]")
{
    ResetFake();
    Owner owner;
    BindCurrent(owner);

    REQUIRE(
        owner.Reserve(MakeIdentity("DrainRetire.ess")).State.Code ==
        StateCode::Reserved);

    g_fake.CancelStatus = Status::InvalidState;
    g_fake.PollStatus = Status::CompletionAvailable;

    const auto drained = owner.DrainShutdown(1u);
    REQUIRE(drained.Status == ShutdownDrainStatus::Completed);
    REQUIRE(drained.IsSafeToTeardown());
    REQUIRE(drained.PollCalls == 1u);
    REQUIRE(drained.KnownPendingPolls == 0u);
    REQUIRE(drained.Last.State.Code == StateCode::Retired);
    REQUIRE(g_fake.CancelCalls == 1u);
    REQUIRE_FALSE(g_fake.CancelSawGenerationLease);
    REQUIRE(g_fake.PollCalls == 1u);
    REQUIRE_FALSE(g_fake.PollSawGenerationLease);
    REQUIRE(g_fake.RetireCalls == 1u);
    REQUIRE_FALSE(g_fake.RetireSawGenerationLease);
}

TEST_CASE(
    "Shutdown drain completes retained old request while lifecycle generation ticket is still pending",
    "[quest.party-state][native-load-owner-core][shutdown][driver][generation]")
{
    ResetFake();
    Owner owner;
    BindCurrent(owner);

    REQUIRE(
        owner.Reserve(MakeIdentity("DrainGeneration.ess")).State.Code ==
        StateCode::Reserved);
    const auto before = owner.Snapshot();
    REQUIRE(before.Phase == Phase::Bound);
    const uint64_t oldGeneration = before.BoundGeneration;

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const auto ticket = fence.BeginLifecycleTransition();
    REQUIRE(ticket.IsValid());
    REQUIRE(ticket.Generation != oldGeneration);

    const auto observed = owner.ObserveGeneration(ticket.Generation);
    REQUIRE(observed.Status == OwnerStatus::Applied);
    REQUIRE(observed.State.Code == StateCode::DrainPending);
    REQUIRE(owner.Snapshot().Phase == Phase::DrainOnly);
    REQUIRE(owner.Snapshot().BoundGeneration == oldGeneration);
    REQUIRE(owner.Snapshot().CurrentGeneration == ticket.Generation);

    g_fake.CancelStatus = Status::InvalidState;
    g_fake.PollStatus = Status::CompletionAvailable;
    const auto drained = owner.DrainShutdown(1u);

    REQUIRE(drained.Status == ShutdownDrainStatus::Completed);
    REQUIRE(drained.IsSafeToTeardown());
    REQUIRE_FALSE(drained.MustRetainCapability());
    REQUIRE(drained.Last.State.Code == StateCode::Retired);
    REQUIRE(drained.FinalPhase == Phase::ShutdownComplete);
    REQUIRE(g_fake.CancelCalls == 1u);
    REQUIRE_FALSE(g_fake.CancelSawGenerationLease);
    REQUIRE(g_fake.PollCalls == 1u);
    REQUIRE_FALSE(g_fake.PollSawGenerationLease);
    REQUIRE(g_fake.RetireCalls == 1u);
    REQUIRE_FALSE(g_fake.RetireSawGenerationLease);

    // The load bridge drain does not complete or mint lifecycle authority.
    REQUIRE(
        PartyQuestRuntimeGenerationFence::GetProcessFence().
            HasPendingLifecycleTransition());
    REQUIRE(fence.CompleteLifecycleTransition(ticket));
}

TEST_CASE(
    "Shutdown drain unknown SEH outcome is terminal unsafe and retains capability",
    "[quest.party-state][native-load-owner-core][shutdown][driver][seh]")
{
    ResetFake();
    Owner owner;
    BindCurrent(owner);

    REQUIRE(
        owner.Reserve(MakeIdentity("DrainSeh.ess")).State.Code ==
        StateCode::Reserved);

    g_fake.CancelRaiseSeh = true;
    const auto failed = owner.DrainShutdown(4u);

    REQUIRE(
        failed.Status ==
        ShutdownDrainStatus::PoisonedUnsafeToUnload);
    REQUIRE_FALSE(failed.IsSafeToTeardown());
    REQUIRE(failed.MustRetainCapability());
    REQUIRE(failed.PollCalls == 0u);
    REQUIRE(failed.KnownPendingPolls == 0u);
    REQUIRE(failed.FinalPhase == Phase::PoisonedUnsafeToUnload);
    REQUIRE(failed.FinalRequestPhase == RequestPhase::Active);
    REQUIRE(failed.Last.State.Code ==
        StateCode::PoisonedUnsafeToUnload);
    REQUIRE(g_fake.CancelCalls == 1u);
    REQUIRE(g_fake.PollCalls == 0u);
    REQUIRE(g_fake.RetireCalls == 0u);
}

TEST_CASE(
    "Shutdown drain rejects same-thread nested driver reentry without deadlock",
    "[quest.party-state][native-load-owner-core][shutdown][driver][reentrant]")
{
    ResetFake();
    Owner owner;
    BindCurrent(owner);

    REQUIRE(
        owner.Reserve(MakeIdentity("DrainReentrant.ess")).State.Code ==
        StateCode::Reserved);

    g_fake.CancelStatus = Status::Cancelled;
    g_fake.ReentrantCancelDrainOwner = &owner;
    const auto drained = owner.DrainShutdown(1u);
    g_fake.ReentrantCancelDrainOwner = nullptr;

    REQUIRE(
        g_fake.ReentrantCancelDrainStatus ==
        ShutdownDrainStatus::LifecycleDeferred);
    REQUIRE(drained.Status == ShutdownDrainStatus::Completed);
    REQUIRE(drained.IsSafeToTeardown());
    REQUIRE(g_fake.CancelCalls == 1u);
}

TEST_CASE(
    "Foreign Reserve can request shutdown reentrantly without owner mutex deadlock",
    "[quest.party-state][native-load-owner-core][reentrant][mutex]")
{
    ResetFake();
    Owner owner;
    BindCurrent(owner);

    g_fake.ReentrantOwner = &owner;
    g_fake.ReentrantGeneration =
        PartyQuestRuntimeGenerationFence::GetProcessFence().GetGeneration();
    const auto reserved =
        owner.Reserve(MakeIdentity("ManualSave45.ess"));
    g_fake.ReentrantOwner = nullptr;

    REQUIRE(reserved.Status == OwnerStatus::Applied);
    REQUIRE(reserved.State.Code == StateCode::Reserved);
    REQUIRE(g_fake.ReentrantObserveStatus ==
        OwnerStatus::LifecycleDeferred);
    REQUIRE(g_fake.ReentrantShutdownStatus ==
        OwnerStatus::LifecycleDeferred);
    REQUIRE(owner.IsShutdownRequested());
    REQUIRE_FALSE(owner.IsOperationActive());

    // Reentrant shutdown only closes admission. It cannot release the active
    // request while the exact Reserve foreign call is still in flight.
    REQUIRE(owner.Snapshot().CapabilityRetained == 1u);
    REQUIRE(owner.Snapshot().RequestPhase == RequestPhase::Active);

    g_fake.CancelStatus = Status::Cancelled;
    const auto shutdown = owner.Shutdown();
    REQUIRE(shutdown.State.Code == StateCode::Cancelled);
    REQUIRE(shutdown.State.ReleaseCapability == 1u);
    REQUIRE(owner.Snapshot().Phase == Phase::ShutdownComplete);
}

TEST_CASE(
    "Impossible foreign status poisons owner state and never releases pinned capability",
    "[quest.party-state][native-load-owner-core][poison]")
{
    ResetFake();
    Owner owner;
    BindCurrent(owner);

    // The module lease tests cover C++/SEH containment directly. Here force an
    // impossible known-return status and prove owner/reducer release semantics.
    g_fake.CancelStatus = Status::Poisoned;

    const auto reserved =
        owner.Reserve(MakeIdentity("ManualSave46.ess"));
    REQUIRE(reserved.State.Code == StateCode::Reserved);
    const uint64_t nonce = owner.Snapshot().ActiveAttemptNonce;

    const auto poisoned = owner.Cancel(nonce);
    REQUIRE(poisoned.Status == OwnerStatus::Applied);
    REQUIRE(poisoned.ForeignCallAttempted == 1u);
    REQUIRE(poisoned.State.Code ==
        StateCode::PoisonedUnsafeToUnload);
    REQUIRE(poisoned.State.ReleaseCapability == 0u);
    REQUIRE(owner.Snapshot().Phase ==
        Phase::PoisonedUnsafeToUnload);
    REQUIRE(owner.Snapshot().CapabilityRetained == 1u);

    const auto after = owner.Poll(nonce);
    REQUIRE(after.State.Code ==
        StateCode::PoisonedUnsafeToUnload);
    REQUIRE(after.ForeignCallAttempted == 0u);
}
#endif
