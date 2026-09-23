#include <Games/Skyrim/PartyQuestSkyrimNativeLoadBridgeModuleLease.h>

#include <Structs/Skyrim/PartyQuestNativeLoadBridgeOwnerState.h>

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
using Lease = PartyQuestSkyrimNativeLoadBridgeModuleLease;
using CreateStatus =
    PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus;
using LifetimeKind =
    PartyQuestSkyrimNativeLoadBridgeLeaseLifetimeKind;
using Owner = PartyQuestNativeLoadBridgeOwnerState;
using Policy = PartyQuestNativeLoadBridgeCallCapabilityPolicy;
using Command = PartyQuestNativeLoadBridgeOwnerCommand;
using CommandKind = PartyQuestNativeLoadBridgeOwnerCommandKind;
using BindDisposition = PartyQuestNativeLoadBridgeOwnerBindDisposition;
using EffectKind = PartyQuestNativeLoadBridgeOwnerEffectKind;
using ForeignDisposition =
    PartyQuestNativeLoadBridgeOwnerForeignDisposition;
using ResultCode = PartyQuestNativeLoadBridgeOwnerResultCode;
using Status = PartyQuestNativeLoadBridgeStatus;
using Identity = PartyQuestNativeLoadBridgeIdentityV1;

constexpr uint64_t kFingerprint = 0x51514C4F41444C31ull;

struct FakeNativeState final
{
    uint32_t ReserveCalls{};
    uint32_t CancelCalls{};
    uint32_t PollCalls{};
    uint32_t RetireCalls{};

    uint64_t NextNonce{41u};
    uint64_t CompletionSequence{71u};
    Status CancelStatus{Status::InvalidState};
    Status PollStatus{Status::Pending};
    Status RetireStatus{Status::Retired};
    uint8_t CompletionResult{1u};
    Identity IdentityValue;
};

FakeNativeState g_fake;

void ResetFake() noexcept
{
    g_fake = {};
    g_fake.NextNonce = 41u;
    g_fake.CompletionSequence = 71u;
    g_fake.CancelStatus = Status::InvalidState;
    g_fake.PollStatus = Status::Pending;
    g_fake.RetireStatus = Status::Retired;
    g_fake.CompletionResult = 1u;
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
    g_fake.IdentityValue = apRequest->Identity;
    return static_cast<uint32_t>(Status::Reserved);
}

uint32_t FakeReserveThrow(
    const PartyQuestNativeLoadBridgeReserveRequestV1*,
    uint32_t,
    PartyQuestNativeLoadBridgeReservationV1*,
    uint32_t)
{
    throw std::runtime_error("foreign C++ exception");
}

uint32_t FakeReserveSeh(
    const PartyQuestNativeLoadBridgeReserveRequestV1*,
    uint32_t,
    PartyQuestNativeLoadBridgeReservationV1*,
    uint32_t)
{
    ::RaiseException(0xE0425151u, 0u, 0u, nullptr);
    return static_cast<uint32_t>(Status::InternalFailure);
}

uint32_t FakeCancel(uint64_t)
{
    ++g_fake.CancelCalls;
    return static_cast<uint32_t>(g_fake.CancelStatus);
}

uint32_t FakePoll(
    uint64_t aAttemptNonce,
    PartyQuestNativeLoadBridgeCompletionV1* apCompletion,
    uint32_t aCompletionSize)
{
    ++g_fake.PollCalls;
    if (!apCompletion || aCompletionSize != sizeof(*apCompletion))
        return static_cast<uint32_t>(Status::InvalidArgument);

    *apCompletion = {};
    if (g_fake.PollStatus == Status::CompletionAvailable)
    {
        apCompletion->AbiVersion =
            kPartyQuestNativeLoadBridgePayloadAbi;
        apCompletion->StructSize = sizeof(*apCompletion);
        apCompletion->AttemptNonce = aAttemptNonce;
        apCompletion->EventSequence = g_fake.CompletionSequence;
        apCompletion->Identity = g_fake.IdentityValue;
        apCompletion->Result = g_fake.CompletionResult;
    }
    return static_cast<uint32_t>(g_fake.PollStatus);
}

uint32_t FakeRetire(uint64_t)
{
    ++g_fake.RetireCalls;
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

bool AcceptAllTestImageAddresses(const uint8_t* apAddress) noexcept
{
    return apAddress != nullptr;
}

const uint8_t* g_rejectedAddress{};

bool RejectOneTestImageAddress(const uint8_t* apAddress) noexcept
{
    return apAddress != nullptr && apAddress != g_rejectedAddress;
}

Command BindCommand(uint64_t aGeneration) noexcept
{
    Command command{};
    command.Kind = CommandKind::Bind;
    command.BindDisposition = BindDisposition::Authenticated;
    command.RuntimeGeneration = aGeneration;
    command.RuntimeFingerprint = kFingerprint;
    return command;
}

Command ReserveCommand(const Identity& acIdentity) noexcept
{
    Command command{};
    command.Kind = CommandKind::Reserve;
    command.Identity = acIdentity;
    return command;
}

Command NonceCommand(CommandKind aKind, uint64_t aNonce) noexcept
{
    Command command{};
    command.Kind = aKind;
    command.AttemptNonce = aNonce;
    return command;
}

Command GenerationCommand(uint64_t aGeneration) noexcept
{
    Command command{};
    command.Kind = CommandKind::GenerationTransition;
    command.RuntimeGeneration = aGeneration;
    return command;
}

PartyQuestNativeLoadBridgeCallCapability Authorize(
    const Owner& acOwner,
    const PartyQuestNativeLoadBridgeOwnerResult& acPlan)
{
    const auto authorized =
        Policy::Authorize(acOwner.Snapshot(), acPlan);
    REQUIRE(authorized.IsAuthorized());
    return authorized.Capability;
}

Lease CreateLease(
    uint64_t aGeneration,
    PartyQuestNativeLoadBridgeReserveExport apReserve,
    CreateStatus& aStatus);
} // namespace

class PartyQuestSkyrimNativeLoadBridgeModuleLeaseTestAccess final
{
public:
    static PartyQuestSkyrimNativeLoadBridgeModuleLease Create(
        void* apExpectedModule,
        uint64_t aGeneration,
        uint64_t aFingerprint,
        PartyQuestNativeLoadBridgeGetDescriptorExport apGetDescriptor,
        PartyQuestNativeLoadBridgeReserveExport apReserve,
        PartyQuestNativeLoadBridgeCancelExport apCancel,
        PartyQuestNativeLoadBridgePollExport apPoll,
        PartyQuestNativeLoadBridgeRetireExport apRetire,
        PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus&
            aStatus) noexcept
    {
        return PartyQuestSkyrimNativeLoadBridgeModuleLease::
            CreateAuthenticated(
                apExpectedModule,
                aGeneration,
                aFingerprint,
                apGetDescriptor,
                apReserve,
                apCancel,
                apPoll,
                apRetire,
                aStatus);
    }

    static PartyQuestSkyrimNativeLoadBridgeModuleLease
    CreateProcessImage(
        uint64_t aGeneration,
        uint64_t aFingerprint,
        PartyQuestSkyrimNativeLoadBridgeImageAddressValidator apValidator,
        PartyQuestNativeLoadBridgeGetDescriptorExport apGetDescriptor,
        PartyQuestNativeLoadBridgeReserveExport apReserve,
        PartyQuestNativeLoadBridgeCancelExport apCancel,
        PartyQuestNativeLoadBridgePollExport apPoll,
        PartyQuestNativeLoadBridgeRetireExport apRetire,
        PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus&
            aStatus) noexcept
    {
        return PartyQuestSkyrimNativeLoadBridgeModuleLease::
            CreateProcessImageAuthenticated(
                aGeneration,
                aFingerprint,
                apValidator,
                apGetDescriptor,
                apReserve,
                apCancel,
                apPoll,
                apRetire,
                aStatus);
    }

    static PartyQuestNativeLoadBridgeOwnerForeignOutcome Execute(
        PartyQuestSkyrimNativeLoadBridgeModuleLease& aLease,
        const PartyQuestNativeLoadBridgeCallCapability& acCapability,
        const PartyQuestNativeLoadBridgeOwnerEffect& acEffect) noexcept
    {
        return aLease.Execute(acCapability, acEffect);
    }
};

namespace
{
Lease CreateLease(
    uint64_t aGeneration,
    PartyQuestNativeLoadBridgeReserveExport apReserve,
    CreateStatus& aStatus)
{
    return PartyQuestSkyrimNativeLoadBridgeModuleLeaseTestAccess::Create(
        ModuleFor(&FakeGetDescriptor),
        aGeneration,
        kFingerprint,
        &FakeGetDescriptor,
        apReserve,
        &FakeCancel,
        &FakePoll,
        &FakeRetire,
        aStatus);
}
} // namespace
#endif

TEST_CASE(
    "Native load module lease exposes only move-only pinned ownership",
    "[quest.party-state][native-load-module-lease][abi]")
{
    using Lease = PartyQuestSkyrimNativeLoadBridgeModuleLease;
    using CreateStatus =
        PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus;

    STATIC_REQUIRE(sizeof(CreateStatus) == 1u);
    STATIC_REQUIRE_FALSE(std::is_default_constructible_v<Lease>);
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<Lease>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<Lease>);
    STATIC_REQUIRE(std::is_nothrow_move_constructible_v<Lease>);
    STATIC_REQUIRE_FALSE(std::is_move_assignable_v<Lease>);
}

#if defined(_WIN32)
TEST_CASE(
    "Native load module lease pins one exact export image",
    "[quest.party-state][native-load-module-lease][pin]")
{
    ResetFake();

    CreateStatus status{};
    auto lease = CreateLease(10u, &FakeReserve, status);
    REQUIRE(status == CreateStatus::Ready);
    REQUIRE(lease.IsPinned());
    REQUIRE(lease.IsCallable());
    REQUIRE(
        lease.GetLifetimeKind() ==
        LifetimeKind::ExternalPinnedModule);
    REQUIRE_FALSE(lease.IsPoisoned());
    REQUIRE(lease.GetBoundGeneration() == 10u);
    REQUIRE(lease.GetRuntimeFingerprint() == kFingerprint);

    auto moved = std::move(lease);
    REQUIRE_FALSE(lease.IsPinned());
    REQUIRE_FALSE(lease.IsCallable());
    REQUIRE(moved.IsPinned());
    REQUIRE(moved.IsCallable());

    CreateStatus mismatchStatus{};
    auto mismatch =
        PartyQuestSkyrimNativeLoadBridgeModuleLeaseTestAccess::Create(
            reinterpret_cast<void*>(::GetModuleHandleW(L"kernel32.dll")),
            10u,
            kFingerprint,
            &FakeGetDescriptor,
            &FakeReserve,
            &FakeCancel,
            &FakePoll,
            &FakeRetire,
            mismatchStatus);
    REQUIRE(mismatchStatus == CreateStatus::ExportModuleMismatch);
    REQUIRE_FALSE(mismatch.IsPinned());
    REQUIRE_FALSE(mismatch.IsCallable());

    CreateStatus invalidStatus{};
    auto invalid =
        PartyQuestSkyrimNativeLoadBridgeModuleLeaseTestAccess::Create(
            ModuleFor(&FakeGetDescriptor),
            0u,
            kFingerprint,
            &FakeGetDescriptor,
            &FakeReserve,
            &FakeCancel,
            &FakePoll,
            &FakeRetire,
            invalidStatus);
    REQUIRE(invalidStatus == CreateStatus::InvalidArgument);
    REQUIRE_FALSE(invalid.IsPinned());
}

TEST_CASE(
    "Native load module lease accepts trusted process-image call targets without PE revalidation",
    "[quest.party-state][native-load-module-lease][process-image]")
{
    ResetFake();

    CreateStatus status{};
    auto lease =
        PartyQuestSkyrimNativeLoadBridgeModuleLeaseTestAccess::
            CreateProcessImage(
                15u,
                kFingerprint,
                &AcceptAllTestImageAddresses,
                &FakeGetDescriptor,
                &FakeReserve,
                &FakeCancel,
                &FakePoll,
                &FakeRetire,
                status);

    REQUIRE(status == CreateStatus::Ready);
    REQUIRE(lease.IsPinned());
    REQUIRE(lease.IsCallable());
    REQUIRE_FALSE(lease.IsPoisoned());
    REQUIRE(
        lease.GetLifetimeKind() == LifetimeKind::ProcessImage);
    REQUIRE(lease.GetBoundGeneration() == 15u);
    REQUIRE(lease.GetRuntimeFingerprint() == kFingerprint);

    auto moved = std::move(lease);
    REQUIRE_FALSE(lease.IsPinned());
    REQUIRE(
        lease.GetLifetimeKind() == LifetimeKind::None);
    REQUIRE(moved.IsPinned());
    REQUIRE(
        moved.GetLifetimeKind() == LifetimeKind::ProcessImage);
}

TEST_CASE(
    "Native load process-image lease requires every exact call target to pass trusted image predicate",
    "[quest.party-state][native-load-module-lease][process-image][origin]")
{
    ResetFake();

    g_rejectedAddress =
        reinterpret_cast<const uint8_t*>(&FakePoll);
    CreateStatus status{};
    auto rejected =
        PartyQuestSkyrimNativeLoadBridgeModuleLeaseTestAccess::
            CreateProcessImage(
                16u,
                kFingerprint,
                &RejectOneTestImageAddress,
                &FakeGetDescriptor,
                &FakeReserve,
                &FakeCancel,
                &FakePoll,
                &FakeRetire,
                status);
    g_rejectedAddress = nullptr;

    REQUIRE(status == CreateStatus::ProcessImageRejected);
    REQUIRE_FALSE(rejected.IsPinned());
    REQUIRE_FALSE(rejected.IsCallable());
    REQUIRE(
        rejected.GetLifetimeKind() == LifetimeKind::None);

    CreateStatus invalidStatus{};
    auto invalid =
        PartyQuestSkyrimNativeLoadBridgeModuleLeaseTestAccess::
            CreateProcessImage(
                16u,
                kFingerprint,
                nullptr,
                &FakeGetDescriptor,
                &FakeReserve,
                &FakeCancel,
                &FakePoll,
                &FakeRetire,
                invalidStatus);
    REQUIRE(invalidStatus == CreateStatus::InvalidArgument);
    REQUIRE_FALSE(invalid.IsPinned());
}

TEST_CASE(
    "Native load module lease executes exact old-generation drain but rejects new-generation authority",
    "[quest.party-state][native-load-module-lease][drain][generation]")
{
    ResetFake();

    CreateStatus status{};
    auto lease = CreateLease(20u, &FakeReserve, status);
    REQUIRE(status == CreateStatus::Ready);

    Owner owner;
    REQUIRE(owner.Plan(BindCommand(20u)).Code == ResultCode::Bound);

    const auto identity = MakeIdentity("ManualSave31.ess");
    const auto reservePlan = owner.Plan(ReserveCommand(identity));
    const auto reserveCapability = Authorize(owner, reservePlan);
    const auto reserved =
        PartyQuestSkyrimNativeLoadBridgeModuleLeaseTestAccess::Execute(
            lease, reserveCapability, reservePlan.Effect);

    REQUIRE(reserved.Disposition == ForeignDisposition::Returned);
    REQUIRE(reserved.EffectSequence == reservePlan.Effect.Sequence);
    REQUIRE(reserved.RawStatus ==
        static_cast<uint32_t>(Status::Reserved));
    REQUIRE(g_fake.ReserveCalls == 1u);

    REQUIRE(owner.ApplyForeignOutcome(reserved).Code ==
        ResultCode::Reserved);
    const uint64_t nonce = owner.Snapshot().ActiveAttemptNonce;
    REQUIRE(nonce == g_fake.NextNonce);

    REQUIRE(owner.Plan(GenerationCommand(21u)).Code ==
        ResultCode::DrainPending);

    const auto cancelPlan =
        owner.Plan(NonceCommand(CommandKind::Cancel, nonce));
    const auto cancelCapability = Authorize(owner, cancelPlan);
    REQUIRE(cancelCapability.IsRequestDrain());
    REQUIRE_FALSE(cancelCapability.AuthorizesRuntimeGeneration(20u));
    REQUIRE_FALSE(cancelCapability.AuthorizesRuntimeGeneration(21u));

    g_fake.CancelStatus = Status::InvalidState;
    const auto cancel =
        PartyQuestSkyrimNativeLoadBridgeModuleLeaseTestAccess::Execute(
            lease, cancelCapability, cancelPlan.Effect);
    REQUIRE(cancel.RawStatus ==
        static_cast<uint32_t>(Status::InvalidState));
    REQUIRE(owner.ApplyForeignOutcome(cancel).Code ==
        ResultCode::Pending);
    REQUIRE(g_fake.CancelCalls == 1u);

    const auto pollPlan =
        owner.Plan(NonceCommand(CommandKind::Poll, nonce));
    const auto pollCapability = Authorize(owner, pollPlan);
    REQUIRE(pollCapability.IsRequestDrain());

    g_fake.PollStatus = Status::CompletionAvailable;
    g_fake.CompletionResult = 0u;
    const auto completion =
        PartyQuestSkyrimNativeLoadBridgeModuleLeaseTestAccess::Execute(
            lease, pollCapability, pollPlan.Effect);
    REQUIRE(completion.RawStatus ==
        static_cast<uint32_t>(Status::CompletionAvailable));
    REQUIRE(completion.HasCompletion == 1u);
    REQUIRE(completion.Completion.Result == 0u);
    REQUIRE(owner.ApplyForeignOutcome(completion).Code ==
        ResultCode::CompletionAvailable);

    const auto retirePlan =
        owner.Plan(NonceCommand(CommandKind::Retire, nonce));
    const auto retireCapability = Authorize(owner, retirePlan);
    const auto retired =
        PartyQuestSkyrimNativeLoadBridgeModuleLeaseTestAccess::Execute(
            lease, retireCapability, retirePlan.Effect);
    const auto retiredApplied =
        owner.ApplyForeignOutcome(retired);
    REQUIRE(retiredApplied.Code == ResultCode::Retired);
    REQUIRE(retiredApplied.ReleaseCapability == 1u);
    REQUIRE(g_fake.RetireCalls == 1u);
    REQUIRE(lease.IsCallable());

    REQUIRE(owner.Plan(BindCommand(21u)).Code == ResultCode::Bound);
    const auto newPlan =
        owner.Plan(ReserveCommand(MakeIdentity("ManualSave32.ess")));
    const auto newCapability = Authorize(owner, newPlan);

    const uint32_t callsBefore = g_fake.ReserveCalls;
    const auto rejected =
        PartyQuestSkyrimNativeLoadBridgeModuleLeaseTestAccess::Execute(
            lease, newCapability, newPlan.Effect);
    REQUIRE(rejected.Disposition == ForeignDisposition::Unknown);
    REQUIRE(rejected.EffectSequence == newPlan.Effect.Sequence);
    REQUIRE(g_fake.ReserveCalls == callsBefore);
    REQUIRE(lease.IsPinned());
    REQUIRE(lease.IsPoisoned());
    REQUIRE_FALSE(lease.IsCallable());

    const auto poisoned = owner.ApplyForeignOutcome(rejected);
    REQUIRE(poisoned.Code == ResultCode::PoisonedUnsafeToUnload);
    REQUIRE(poisoned.ReleaseCapability == 0u);
}

TEST_CASE(
    "Native load module lease rejects effect substitution before foreign call",
    "[quest.party-state][native-load-module-lease][correlation]")
{
    ResetFake();

    SECTION("effect sequence")
    {
        CreateStatus status{};
        auto lease = CreateLease(30u, &FakeReserve, status);
        REQUIRE(status == CreateStatus::Ready);

        Owner owner;
        REQUIRE(owner.Plan(BindCommand(30u)).Code ==
            ResultCode::Bound);
        const auto plan =
            owner.Plan(ReserveCommand(MakeIdentity("SaveA.ess")));
        const auto capability = Authorize(owner, plan);

        auto substituted = plan.Effect;
        ++substituted.Sequence;
        const auto outcome =
            PartyQuestSkyrimNativeLoadBridgeModuleLeaseTestAccess::Execute(
                lease, capability, substituted);
        REQUIRE(outcome.Disposition == ForeignDisposition::Unknown);
        REQUIRE(g_fake.ReserveCalls == 0u);
        REQUIRE(lease.IsPoisoned());
    }

    SECTION("reserve identity")
    {
        ResetFake();
        CreateStatus status{};
        auto lease = CreateLease(31u, &FakeReserve, status);
        REQUIRE(status == CreateStatus::Ready);

        Owner owner;
        REQUIRE(owner.Plan(BindCommand(31u)).Code ==
            ResultCode::Bound);
        const auto plan =
            owner.Plan(ReserveCommand(MakeIdentity("SaveA.ess")));
        const auto capability = Authorize(owner, plan);

        auto substituted = plan.Effect;
        substituted.ReserveRequest.Identity.Bytes[0] ^= 0x01u;
        const auto outcome =
            PartyQuestSkyrimNativeLoadBridgeModuleLeaseTestAccess::Execute(
                lease, capability, substituted);
        REQUIRE(outcome.Disposition == ForeignDisposition::Unknown);
        REQUIRE(g_fake.ReserveCalls == 0u);
        REQUIRE(lease.IsPoisoned());
    }
}

TEST_CASE(
    "Native load module lease contains C++ and SEH foreign failures",
    "[quest.party-state][native-load-module-lease][foreign-failure]")
{
    for (const auto reserve : {
             PartyQuestNativeLoadBridgeReserveExport(&FakeReserveThrow),
             PartyQuestNativeLoadBridgeReserveExport(&FakeReserveSeh)})
    {
        ResetFake();
        CreateStatus status{};
        auto lease = CreateLease(40u, reserve, status);
        REQUIRE(status == CreateStatus::Ready);

        Owner owner;
        REQUIRE(owner.Plan(BindCommand(40u)).Code ==
            ResultCode::Bound);
        const auto plan =
            owner.Plan(ReserveCommand(MakeIdentity("Failure.ess")));
        const auto capability = Authorize(owner, plan);

        const auto outcome =
            PartyQuestSkyrimNativeLoadBridgeModuleLeaseTestAccess::Execute(
                lease, capability, plan.Effect);
        REQUIRE(outcome.Disposition == ForeignDisposition::Unknown);
        REQUIRE(outcome.EffectKind == EffectKind::Reserve);
        REQUIRE(outcome.EffectSequence == plan.Effect.Sequence);
        REQUIRE(lease.IsPinned());
        REQUIRE(lease.IsPoisoned());
        REQUIRE_FALSE(lease.IsCallable());

        const auto applied = owner.ApplyForeignOutcome(outcome);
        REQUIRE(applied.Code == ResultCode::PoisonedUnsafeToUnload);
        REQUIRE(applied.ReleaseCapability == 0u);
        REQUIRE(owner.Snapshot().CapabilityRetained == 1u);
    }
}
#endif
