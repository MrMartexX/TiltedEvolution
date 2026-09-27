#include <Games/Skyrim/PartyQuestSkyrimNativeLoadBridgeResolver.h>

#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>

#include <catch2/catch.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <type_traits>
#include <utility>

#if defined(_WIN32)
#include <Windows.h>

namespace
{
using ResolveStatus = PartyQuestSkyrimNativeLoadBridgeResolveStatus;
using ResolveResult = PartyQuestSkyrimNativeLoadBridgeResolveResult;
using BindResult = PartyQuestSkyrimNativeLoadBridgeBindResult;
using BindStatus = PartyQuestSkyrimNativeLoadBridgeBindStatus;
using Owner = PartyQuestSkyrimNativeLoadBridgeOwner;
using OwnerStatus = PartyQuestSkyrimNativeLoadBridgeOwnerStatus;
using OwnerPhase = PartyQuestNativeLoadBridgeOwnerPhase;
using OwnerStateCode = PartyQuestNativeLoadBridgeOwnerResultCode;
using SourceAuthorization =
    PartyQuestSkyrimNativeLoadBridgeSourceAuthorization;
using RuntimeAuthorization =
    PartyQuestSkyrimRuntimeIdentityAuthorization;
using RuntimeVersion = PartyQuestSkyrimRuntimeVersion;
using ExecutableIdentity = PartyQuestSkyrimExecutableIdentity;
using DescriptorResult = PartyQuestNativeLoadBridgeDescriptorResult;
using BridgeStatus = PartyQuestNativeLoadBridgeStatus;
using LifetimeKind =
    PartyQuestSkyrimNativeLoadBridgeLeaseLifetimeKind;

constexpr RuntimeVersion kRuntime{1u, 6u, 1170u, 0u};

constexpr ExecutableIdentity MakeExecutableIdentity(uint8_t aSeed) noexcept
{
    ExecutableIdentity identity{};
    identity.Sha256[0] = aSeed == 0u ? 1u : aSeed;
    identity.Sha256[31] =
        static_cast<uint8_t>(identity.Sha256[0] ^ 0xA5u);
    return identity;
}

constexpr ExecutableIdentity kFixtureExecutableIdentity =
    MakeExecutableIdentity(0x42u);
constexpr uint64_t kRuntimeFingerprint =
    PartyQuestNativeLoadBridgeRuntimeFingerprintPolicy::Derive(
        kRuntime,
        kFixtureExecutableIdentity);
static_assert(kRuntimeFingerprint != 0u);

enum class FakeDescriptorMode : uint8_t
{
    Valid,
    Unavailable,
    BadFingerprint,
    ThrowCpp,
    RaiseSeh
};

FakeDescriptorMode g_descriptorMode{FakeDescriptorMode::Valid};
uint32_t g_descriptorCalls{};

PartyQuestNativeLoadBridgeDescriptorV1 MakeDescriptor() noexcept
{
    PartyQuestNativeLoadBridgeDescriptorV1 descriptor{};
    descriptor.AbiVersion = kPartyQuestNativeLoadBridgeDescriptorAbi;
    descriptor.StructSize = sizeof(descriptor);
    descriptor.PayloadAbiVersion = kPartyQuestNativeLoadBridgePayloadAbi;
    descriptor.ImplementationVersion =
        kPartyQuestNativeLoadBridgeImplementationVersion;
    descriptor.Capabilities =
        kPartyQuestRequiredNativeLoadBridgeCapabilities;
    descriptor.RuntimeMajor = kRuntime.Major;
    descriptor.RuntimeMinor = kRuntime.Minor;
    descriptor.RuntimePatch = kRuntime.Patch;
    descriptor.RuntimeBuild = kRuntime.Build;
    descriptor.RuntimeFingerprint = kRuntimeFingerprint;
    descriptor.BridgeFingerprint =
        kPartyQuestNativeLoadBridgeFingerprint;
    return descriptor;
}

std::filesystem::path GetCurrentExecutablePath()
{
    std::wstring buffer(512u, L'\0');
    for (;;)
    {
        const DWORD length = ::GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        REQUIRE(length != 0u);

        if (length < buffer.size() - 1u)
        {
            buffer.resize(length);
            return std::filesystem::path(std::move(buffer));
        }

        REQUIRE(buffer.size() < 32768u);
        buffer.resize(buffer.size() * 2u, L'\0');
    }
}

void ResetFake() noexcept
{
    g_descriptorMode = FakeDescriptorMode::Valid;
    g_descriptorCalls = 0u;
}
} // namespace

extern "C" __declspec(dllexport) uint32_t
PartyQuestSKSE_GetLoadBridgeDescriptor(
    PartyQuestNativeLoadBridgeDescriptorV1* apDescriptor,
    uint32_t aDescriptorSize)
{
    ++g_descriptorCalls;
    switch (g_descriptorMode)
    {
    case FakeDescriptorMode::ThrowCpp:
        throw std::runtime_error("resolver descriptor C++ failure");

    case FakeDescriptorMode::RaiseSeh:
        ::RaiseException(0xE0425152u, 0u, 0u, nullptr);
        return static_cast<uint32_t>(DescriptorResult::Unavailable);

    case FakeDescriptorMode::Unavailable:
        if (apDescriptor &&
            aDescriptorSize == sizeof(*apDescriptor))
        {
            *apDescriptor = {};
        }
        return static_cast<uint32_t>(DescriptorResult::Unavailable);

    case FakeDescriptorMode::BadFingerprint:
    case FakeDescriptorMode::Valid:
        break;
    }

    if (!apDescriptor ||
        aDescriptorSize != sizeof(*apDescriptor))
    {
        return static_cast<uint32_t>(DescriptorResult::Unavailable);
    }

    *apDescriptor = MakeDescriptor();
    if (g_descriptorMode == FakeDescriptorMode::BadFingerprint)
        ++apDescriptor->RuntimeFingerprint;

    return static_cast<uint32_t>(DescriptorResult::Available);
}

extern "C" __declspec(dllexport) uint32_t
PartyQuestSKSE_ReserveLoad(
    const PartyQuestNativeLoadBridgeReserveRequestV1*,
    uint32_t,
    PartyQuestNativeLoadBridgeReservationV1*,
    uint32_t)
{
    return static_cast<uint32_t>(BridgeStatus::InvalidState);
}

extern "C" __declspec(dllexport) uint32_t
PartyQuestSKSE_CancelLoad(uint64_t)
{
    return static_cast<uint32_t>(BridgeStatus::InvalidState);
}

extern "C" __declspec(dllexport) uint32_t
PartyQuestSKSE_PollLoad(
    uint64_t,
    PartyQuestNativeLoadBridgeCompletionV1*,
    uint32_t)
{
    return static_cast<uint32_t>(BridgeStatus::Pending);
}

extern "C" __declspec(dllexport) uint32_t
PartyQuestSKSE_RetireLoad(uint64_t)
{
    return static_cast<uint32_t>(BridgeStatus::Retired);
}

namespace
{
const uint8_t* g_rejectedProcessImageAddress{};

bool AcceptResolverProcessImageAddress(const uint8_t* apAddress) noexcept
{
    return apAddress != nullptr;
}

bool RejectOneResolverProcessImageAddress(const uint8_t* apAddress) noexcept
{
    return apAddress != nullptr &&
        apAddress != g_rejectedProcessImageAddress;
}

bool RaiseSehResolverProcessImageAddress(const uint8_t*) noexcept
{
    ::RaiseException(0xE0425153u, 0u, 0u, nullptr);
    return false;
}
} // namespace

class PartyQuestSkyrimNativeLoadBridgeResolverTestAccess final
{
public:
    static RuntimeAuthorization MakeRuntimeAuthorization(
        RuntimeVersion aVersion,
        ExecutableIdentity aExecutableIdentity) noexcept
    {
        return PartyQuestSkyrimRuntimeIdentityResolver::ResolveTrustedState(
            aVersion,
            true,
            aVersion,
            true,
            aExecutableIdentity);
    }

    static SourceAuthorization MakeSourceAuthorization(
        RuntimeVersion aVersion,
        ExecutableIdentity aExecutableIdentity,
        std::filesystem::path aRelativeModulePath,
        std::array<uint8_t, 32> aModuleSha256,
        uint64_t aRuntimeFingerprint,
        bool aReviewed = true) noexcept
    {
        return SourceAuthorization(
            aVersion,
            aExecutableIdentity,
            std::move(aRelativeModulePath),
            aModuleSha256,
            aRuntimeFingerprint,
            aReviewed);
    }

    static SourceAuthorization MakeProcessImageSourceAuthorization(
        RuntimeVersion aVersion,
        ExecutableIdentity aExecutableIdentity,
        uint64_t aRuntimeFingerprint,
        PartyQuestSkyrimNativeLoadBridgeImageAddressValidator apValidator,
        PartyQuestNativeLoadBridgeGetDescriptorExport apGetDescriptor,
        PartyQuestNativeLoadBridgeReserveExport apReserve,
        PartyQuestNativeLoadBridgeCancelExport apCancel,
        PartyQuestNativeLoadBridgePollExport apPoll,
        PartyQuestNativeLoadBridgeRetireExport apRetire,
        bool aReviewed = true) noexcept
    {
        return SourceAuthorization(
            aVersion,
            aExecutableIdentity,
            aRuntimeFingerprint,
            apValidator,
            apGetDescriptor,
            apReserve,
            apCancel,
            apPoll,
            apRetire,
            aReviewed);
    }

    static bool HashFile(
        const std::filesystem::path& acPath,
        std::array<uint8_t, 32>& aHash) noexcept
    {
        return PartyQuestSkyrimNativeLoadBridgeResolver::HashFileSha256(
            acPath,
            aHash);
    }

    static ResolveResult ResolveAndPin(
        const std::filesystem::path& acTrustedGameDirectory,
        const RuntimeAuthorization& acRuntimeIdentity,
        const SourceAuthorization& acSource,
        uint64_t aExpectedGeneration) noexcept
    {
        return PartyQuestSkyrimNativeLoadBridgeResolver::ResolveAndPin(
            acTrustedGameDirectory,
            acRuntimeIdentity,
            acSource,
            aExpectedGeneration);
    }

    static ResolveResult ResolveProcessImageAndPin(
        const RuntimeAuthorization& acRuntimeIdentity,
        const SourceAuthorization& acSource,
        uint64_t aExpectedGeneration) noexcept
    {
        return PartyQuestSkyrimNativeLoadBridgeResolver::
            ResolveProcessImageAndPin(
                acRuntimeIdentity,
                acSource,
                aExpectedGeneration);
    }

    static BindResult BindResolved(
        Owner& aOwner,
        ResolveResult&& aResolved) noexcept
    {
        return PartyQuestSkyrimNativeLoadBridgeResolver::BindResolved(
            aOwner,
            std::move(aResolved));
    }
};

namespace
{
struct TrustedFixture final
{
    std::filesystem::path ExecutablePath;
    std::filesystem::path TrustedDirectory;
    ExecutableIdentity SkyrimExecutableIdentity;
    RuntimeAuthorization Runtime;
    std::array<uint8_t, 32> ModuleSha256{};
    SourceAuthorization Source;
    uint64_t Generation{};

    TrustedFixture()
        : ExecutablePath(GetCurrentExecutablePath())
        , TrustedDirectory(ExecutablePath.parent_path())
        , SkyrimExecutableIdentity(kFixtureExecutableIdentity)
        , Runtime(
              PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::
                  MakeRuntimeAuthorization(
                      kRuntime,
                      SkyrimExecutableIdentity))
        , Source()
    {
        REQUIRE(ExecutablePath.is_absolute());
        REQUIRE_FALSE(TrustedDirectory.empty());
        REQUIRE(Runtime.IsVerified());
        REQUIRE(Runtime.GetExecutableIdentity().Matches(
            SkyrimExecutableIdentity));

        REQUIRE(
            PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::HashFile(
                ExecutablePath,
                ModuleSha256));

        Source =
            PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::
                MakeSourceAuthorization(
                    kRuntime,
                    SkyrimExecutableIdentity,
                    ExecutablePath.filename(),
                    ModuleSha256,
                    kRuntimeFingerprint);
        REQUIRE(Source.IsVerified());

        Generation =
            PartyQuestRuntimeGenerationFence::GetProcessFence().
                GetGeneration();
        REQUIRE(Generation != 0u);
    }
};

ResolveResult Resolve(const TrustedFixture& acFixture)
{
    return PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::ResolveAndPin(
        acFixture.TrustedDirectory,
        acFixture.Runtime,
        acFixture.Source,
        acFixture.Generation);
}

SourceAuthorization MakeProcessImageSource(
    const TrustedFixture& acFixture,
    PartyQuestSkyrimNativeLoadBridgeImageAddressValidator apValidator =
        &AcceptResolverProcessImageAddress,
    bool aReviewed = true)
{
    return PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::
        MakeProcessImageSourceAuthorization(
            kRuntime,
            acFixture.SkyrimExecutableIdentity,
            kRuntimeFingerprint,
            apValidator,
            &PartyQuestSKSE_GetLoadBridgeDescriptor,
            &PartyQuestSKSE_ReserveLoad,
            &PartyQuestSKSE_CancelLoad,
            &PartyQuestSKSE_PollLoad,
            &PartyQuestSKSE_RetireLoad,
            aReviewed);
}

ResolveResult ResolveProcessImage(
    const TrustedFixture& acFixture,
    const SourceAuthorization& acSource)
{
    return PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::
        ResolveProcessImageAndPin(
            acFixture.Runtime,
            acSource,
            acFixture.Generation);
}
} // namespace
#endif

TEST_CASE(
    "Native load bridge production source registry remains fail closed without reviewed artifact",
    "[quest.party-state][native-load-resolver][source-registry]")
{
    const PartyQuestSkyrimRuntimeIdentityAuthorization unverifiedRuntime;
    const auto source =
        PartyQuestSkyrimNativeLoadBridgeSourceRegistry::Resolve(
            unverifiedRuntime);
    REQUIRE_FALSE(source.IsVerified());
}

TEST_CASE(
    "Native load bridge resolver result is move-only when it can own pinned lease",
    "[quest.party-state][native-load-resolver][abi]")
{
    using Result = PartyQuestSkyrimNativeLoadBridgeResolveResult;
    using Status = PartyQuestSkyrimNativeLoadBridgeResolveStatus;

    STATIC_REQUIRE(sizeof(Status) == 1u);
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<Result>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<Result>);
    STATIC_REQUIRE(std::is_move_constructible_v<Result>);
}

#if defined(_WIN32)
TEST_CASE(
    "Native load bridge source authorization rejects unreviewed and path traversal evidence",
    "[quest.party-state][native-load-resolver][source-authorization]")
{
    const auto executableIdentity = MakeExecutableIdentity(0x31u);
    const uint64_t exactFingerprint =
        PartyQuestNativeLoadBridgeRuntimeFingerprintPolicy::Derive(
            kRuntime,
            executableIdentity);
    REQUIRE(exactFingerprint != 0u);
    std::array<uint8_t, 32> hash{};
    hash[0] = 1u;

    const auto unreviewed =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::
            MakeSourceAuthorization(
                kRuntime,
                executableIdentity,
                L"native-load-test.dll",
                hash,
                exactFingerprint,
                false);
    REQUIRE_FALSE(unreviewed.IsVerified());

    const auto parentTraversal =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::
            MakeSourceAuthorization(
                kRuntime,
                executableIdentity,
                std::filesystem::path(L"Data") /
                    L".." /
                    L"native-load-test.dll",
                hash,
                exactFingerprint);
    REQUIRE_FALSE(parentTraversal.IsVerified());

    const auto currentTraversal =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::
            MakeSourceAuthorization(
                kRuntime,
                executableIdentity,
                std::filesystem::path(L".") /
                    L"native-load-test.dll",
                hash,
                exactFingerprint);
    REQUIRE_FALSE(currentTraversal.IsVerified());

    const auto absolute =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::
            MakeSourceAuthorization(
                kRuntime,
                executableIdentity,
                std::filesystem::path(L"C:\\native-load-test.dll"),
                hash,
                exactFingerprint);
    REQUIRE_FALSE(absolute.IsVerified());

    auto zeroHash = hash;
    zeroHash.fill(0u);
    const auto noHash =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::
            MakeSourceAuthorization(
                kRuntime,
                executableIdentity,
                L"native-load-test.dll",
                zeroHash,
                exactFingerprint);
    REQUIRE_FALSE(noHash.IsVerified());

    const auto wrongFingerprint =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::
            MakeSourceAuthorization(
                kRuntime,
                executableIdentity,
                L"native-load-test.dll",
                hash,
                exactFingerprint ^ 0x1u);
    REQUIRE_FALSE(wrongFingerprint.IsVerified());

    const auto noFingerprint =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::
            MakeSourceAuthorization(
                kRuntime,
                executableIdentity,
                L"native-load-test.dll",
                hash,
                0u);
    REQUIRE_FALSE(noFingerprint.IsVerified());
}

TEST_CASE(
    "Native load bridge process-image authorization binds exact reviewed call surface",
    "[quest.party-state][native-load-resolver][process-image][authorization]")
{
    ResetFake();
    TrustedFixture fixture;

    const auto source = MakeProcessImageSource(fixture);
    REQUIRE(source.IsVerified());
    REQUIRE(
        source.GetLifetimeKind() == LifetimeKind::ProcessImage);
    REQUIRE(source.GetRelativeModulePath().empty());
    for (const auto value : source.GetModuleSha256())
        REQUIRE(value == 0u);
    REQUIRE(source.GetProcessImageValidator() ==
        &AcceptResolverProcessImageAddress);
    REQUIRE(source.GetProcessImageDescriptor() ==
        &PartyQuestSKSE_GetLoadBridgeDescriptor);
    REQUIRE(source.GetProcessImageReserve() ==
        &PartyQuestSKSE_ReserveLoad);
    REQUIRE(source.GetProcessImageCancel() ==
        &PartyQuestSKSE_CancelLoad);
    REQUIRE(source.GetProcessImagePoll() ==
        &PartyQuestSKSE_PollLoad);
    REQUIRE(source.GetProcessImageRetire() ==
        &PartyQuestSKSE_RetireLoad);

    const auto unreviewed =
        MakeProcessImageSource(
            fixture,
            &AcceptResolverProcessImageAddress,
            false);
    REQUIRE_FALSE(unreviewed.IsVerified());

    const auto missingValidator =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::
            MakeProcessImageSourceAuthorization(
                kRuntime,
                fixture.SkyrimExecutableIdentity,
                kRuntimeFingerprint,
                nullptr,
                &PartyQuestSKSE_GetLoadBridgeDescriptor,
                &PartyQuestSKSE_ReserveLoad,
                &PartyQuestSKSE_CancelLoad,
                &PartyQuestSKSE_PollLoad,
                &PartyQuestSKSE_RetireLoad);
    REQUIRE_FALSE(missingValidator.IsVerified());

    const auto missingPoll =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::
            MakeProcessImageSourceAuthorization(
                kRuntime,
                fixture.SkyrimExecutableIdentity,
                kRuntimeFingerprint,
                &AcceptResolverProcessImageAddress,
                &PartyQuestSKSE_GetLoadBridgeDescriptor,
                &PartyQuestSKSE_ReserveLoad,
                &PartyQuestSKSE_CancelLoad,
                nullptr,
                &PartyQuestSKSE_RetireLoad);
    REQUIRE_FALSE(missingPoll.IsVerified());
}

TEST_CASE(
    "Native load bridge resolver rejects invalid trust inputs before module use",
    "[quest.party-state][native-load-resolver][trust-boundary]")
{
    ResetFake();
    TrustedFixture fixture;

    SECTION("relative trusted root")
    {
        const auto result =
            PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::ResolveAndPin(
                std::filesystem::path(L"."),
                fixture.Runtime,
                fixture.Source,
                fixture.Generation);
        REQUIRE(result.Status == ResolveStatus::InvalidTrustedDirectory);
        REQUIRE_FALSE(result.IsResolved());
    }

    SECTION("unverified runtime identity")
    {
        const RuntimeAuthorization invalidRuntime;
        const auto result =
            PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::ResolveAndPin(
                fixture.TrustedDirectory,
                invalidRuntime,
                fixture.Source,
                fixture.Generation);
        REQUIRE(result.Status == ResolveStatus::RuntimeIdentityRejected);
        REQUIRE_FALSE(result.IsResolved());
    }

    SECTION("unverified source authorization")
    {
        const SourceAuthorization invalidSource;
        const auto result =
            PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::ResolveAndPin(
                fixture.TrustedDirectory,
                fixture.Runtime,
                invalidSource,
                fixture.Generation);
        REQUIRE(result.Status == ResolveStatus::SourceAuthorizationRejected);
        REQUIRE_FALSE(result.IsResolved());
    }

    SECTION("zero generation")
    {
        const auto result =
            PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::ResolveAndPin(
                fixture.TrustedDirectory,
                fixture.Runtime,
                fixture.Source,
                0u);
        REQUIRE(result.Status == ResolveStatus::GenerationUnavailable);
        REQUIRE_FALSE(result.IsResolved());
    }
}

TEST_CASE(
    "Native load bridge resolver authenticates exact loaded artifact and returns pinned lease",
    "[quest.party-state][native-load-resolver][resolve]")
{
    ResetFake();
    TrustedFixture fixture;

    const auto resolved = Resolve(fixture);
    REQUIRE(resolved.Status == ResolveStatus::Resolved);
    REQUIRE(resolved.IsResolved());
    REQUIRE(resolved.Lease);
    REQUIRE(resolved.Lease->IsPinned());
    REQUIRE(resolved.Lease->IsCallable());
    REQUIRE_FALSE(resolved.Lease->IsPoisoned());
    REQUIRE(resolved.Lease->GetBoundGeneration() ==
        fixture.Generation);
    REQUIRE(resolved.Lease->GetRuntimeFingerprint() ==
        kRuntimeFingerprint);

    REQUIRE(
        PartyQuestNativeLoadBridgePolicy::IsApprovedDescriptor(
            resolved.Descriptor,
            kRuntimeFingerprint));
}

TEST_CASE(
    "Native load bridge resolver requires matching trusted runtime and executable identity",
    "[quest.party-state][native-load-resolver][runtime]")
{
    ResetFake();
    TrustedFixture fixture;

    SECTION("runtime version")
    {
        const RuntimeVersion otherRuntime{1u, 6u, 640u, 0u};
        const auto runtime =
            PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::
                MakeRuntimeAuthorization(
                    otherRuntime,
                    fixture.SkyrimExecutableIdentity);
        REQUIRE(runtime.IsVerified());

        const auto result =
            PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::ResolveAndPin(
                fixture.TrustedDirectory,
                runtime,
                fixture.Source,
                fixture.Generation);
        REQUIRE(result.Status == ResolveStatus::RuntimeMismatch);
        REQUIRE_FALSE(result.IsResolved());
    }

    SECTION("executable identity")
    {
        const auto otherIdentity = MakeExecutableIdentity(0x77u);
        const auto runtime =
            PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::
                MakeRuntimeAuthorization(
                    kRuntime,
                    otherIdentity);
        REQUIRE(runtime.IsVerified());

        const auto result =
            PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::ResolveAndPin(
                fixture.TrustedDirectory,
                runtime,
                fixture.Source,
                fixture.Generation);
        REQUIRE(
            result.Status ==
            ResolveStatus::ExecutableIdentityMismatch);
        REQUIRE_FALSE(result.IsResolved());
    }
}

TEST_CASE(
    "Native load bridge resolver rejects backing file hash mismatch",
    "[quest.party-state][native-load-resolver][hash]")
{
    ResetFake();
    TrustedFixture fixture;

    auto wrongHash = fixture.ModuleSha256;
    wrongHash[0] ^= 0x80u;
    const auto source =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::
            MakeSourceAuthorization(
                kRuntime,
                fixture.SkyrimExecutableIdentity,
                fixture.ExecutablePath.filename(),
                wrongHash,
                kRuntimeFingerprint);
    REQUIRE(source.IsVerified());

    const auto result =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::ResolveAndPin(
            fixture.TrustedDirectory,
            fixture.Runtime,
            source,
            fixture.Generation);
    REQUIRE(result.Status == ResolveStatus::ModuleHashMismatch);
    REQUIRE_FALSE(result.IsResolved());
}

TEST_CASE(
    "Native load bridge resolver rejects missing reviewed module without loading it",
    "[quest.party-state][native-load-resolver][module]")
{
    ResetFake();
    TrustedFixture fixture;

    const auto source =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::
            MakeSourceAuthorization(
                kRuntime,
                fixture.SkyrimExecutableIdentity,
                L"partyquest-does-not-exist-9f21.dll",
                fixture.ModuleSha256,
                kRuntimeFingerprint);
    REQUIRE(source.IsVerified());

    const auto result =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::ResolveAndPin(
            fixture.TrustedDirectory,
            fixture.Runtime,
            source,
            fixture.Generation);
    REQUIRE(result.Status == ResolveStatus::ModuleUnavailable);
    REQUIRE_FALSE(result.IsResolved());
}

TEST_CASE(
    "Native load bridge resolver contains descriptor failures and requires exact fingerprint",
    "[quest.party-state][native-load-resolver][descriptor]")
{
    ResetFake();
    TrustedFixture fixture;

    SECTION("unavailable")
    {
        g_descriptorMode = FakeDescriptorMode::Unavailable;
        const auto result = Resolve(fixture);
        REQUIRE(result.Status == ResolveStatus::DescriptorRejected);
        REQUIRE_FALSE(result.IsResolved());
    }

    SECTION("fingerprint mismatch")
    {
        g_descriptorMode = FakeDescriptorMode::BadFingerprint;
        const auto result = Resolve(fixture);
        REQUIRE(result.Status == ResolveStatus::DescriptorRejected);
        REQUIRE_FALSE(result.IsResolved());
    }

    SECTION("C++ exception")
    {
        g_descriptorMode = FakeDescriptorMode::ThrowCpp;
        const auto result = Resolve(fixture);
        REQUIRE(result.Status == ResolveStatus::DescriptorCallFailed);
        REQUIRE_FALSE(result.IsResolved());
    }

    SECTION("SEH exception")
    {
        g_descriptorMode = FakeDescriptorMode::RaiseSeh;
        const auto result = Resolve(fixture);
        REQUIRE(result.Status == ResolveStatus::DescriptorCallFailed);
        REQUIRE_FALSE(result.IsResolved());
    }

    ResetFake();
}

TEST_CASE(
    "Native load bridge resolver authenticates reviewed process-image surface",
    "[quest.party-state][native-load-resolver][process-image][resolve]")
{
    ResetFake();
    TrustedFixture fixture;
    const auto source = MakeProcessImageSource(fixture);

    const auto resolved = ResolveProcessImage(fixture, source);
    REQUIRE(resolved.Status == ResolveStatus::Resolved);
    REQUIRE(resolved.IsResolved());
    REQUIRE(resolved.Lease);
    REQUIRE(resolved.Lease->IsPinned());
    REQUIRE(resolved.Lease->IsCallable());
    REQUIRE(
        resolved.Lease->GetLifetimeKind() ==
        LifetimeKind::ProcessImage);
    REQUIRE(resolved.Lease->GetBoundGeneration() ==
        fixture.Generation);
    REQUIRE(resolved.Lease->GetRuntimeFingerprint() ==
        kRuntimeFingerprint);
}

TEST_CASE(
    "Native load bridge resolver rejects source-kind substitution",
    "[quest.party-state][native-load-resolver][process-image][kind]")
{
    ResetFake();
    TrustedFixture fixture;

    const auto externalOnProcess =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::
            ResolveProcessImageAndPin(
                fixture.Runtime,
                fixture.Source,
                fixture.Generation);
    REQUIRE(
        externalOnProcess.Status ==
        ResolveStatus::SourceKindMismatch);
    REQUIRE_FALSE(externalOnProcess.IsResolved());

    const auto processSource = MakeProcessImageSource(fixture);
    const auto processOnExternal =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::ResolveAndPin(
            fixture.TrustedDirectory,
            fixture.Runtime,
            processSource,
            fixture.Generation);
    REQUIRE(
        processOnExternal.Status ==
        ResolveStatus::SourceKindMismatch);
    REQUIRE_FALSE(processOnExternal.IsResolved());
}

TEST_CASE(
    "Native load bridge resolver rejects process-image address predicate failure before descriptor call",
    "[quest.party-state][native-load-resolver][process-image][origin]")
{
    ResetFake();
    TrustedFixture fixture;

    g_rejectedProcessImageAddress =
        reinterpret_cast<const uint8_t*>(
            &PartyQuestSKSE_PollLoad);
    const auto source =
        MakeProcessImageSource(
            fixture,
            &RejectOneResolverProcessImageAddress);

    const auto rejected = ResolveProcessImage(fixture, source);
    g_rejectedProcessImageAddress = nullptr;

    REQUIRE(
        rejected.Status ==
        ResolveStatus::ProcessImageAddressRejected);
    REQUIRE_FALSE(rejected.IsResolved());
    REQUIRE(g_descriptorCalls == 0u);

    const auto sehSource =
        MakeProcessImageSource(
            fixture,
            &RaiseSehResolverProcessImageAddress);
    const auto sehRejected =
        ResolveProcessImage(fixture, sehSource);
    REQUIRE(
        sehRejected.Status ==
        ResolveStatus::ProcessImageAddressRejected);
    REQUIRE_FALSE(sehRejected.IsResolved());
    REQUIRE(g_descriptorCalls == 0u);
}

TEST_CASE(
    "Native load bridge resolver applies exact descriptor policy to process-image surface",
    "[quest.party-state][native-load-resolver][process-image][descriptor]")
{
    ResetFake();
    TrustedFixture fixture;
    const auto source = MakeProcessImageSource(fixture);

    g_descriptorMode = FakeDescriptorMode::BadFingerprint;
    const auto rejected = ResolveProcessImage(fixture, source);
    REQUIRE(rejected.Status == ResolveStatus::DescriptorRejected);
    REQUIRE_FALSE(rejected.IsResolved());

    g_descriptorMode = FakeDescriptorMode::RaiseSeh;
    const auto faulted = ResolveProcessImage(fixture, source);
    REQUIRE(faulted.Status == ResolveStatus::DescriptorCallFailed);
    REQUIRE_FALSE(faulted.IsResolved());

    ResetFake();
}

TEST_CASE(
    "Native load process-image resolver hands exact lease directly to owner",
    "[quest.party-state][native-load-resolver][process-image][owner-handoff]")
{
    ResetFake();
    TrustedFixture fixture;
    const auto source = MakeProcessImageSource(fixture);
    Owner owner;

    const auto bound =
        PartyQuestSkyrimNativeLoadBridgeResolver::
            ResolveProcessImageAndBind(
                fixture.Runtime,
                source,
                fixture.Generation,
                owner);

    REQUIRE(bound.Status == BindStatus::Bound);
    REQUIRE(bound.IsBound());
    REQUIRE(bound.ResolverStatus == ResolveStatus::Resolved);
    REQUIRE(bound.Owner.Status == OwnerStatus::Applied);
    REQUIRE(bound.Owner.State.Code == OwnerStateCode::Bound);
    REQUIRE(owner.Snapshot().Phase == OwnerPhase::Bound);
    REQUIRE(owner.Snapshot().BoundGeneration ==
        fixture.Generation);

    const auto shutdown = owner.Shutdown();
    REQUIRE(shutdown.Status == OwnerStatus::Applied);
    REQUIRE(
        shutdown.State.Code ==
        OwnerStateCode::ShutdownComplete);
    REQUIRE(shutdown.State.ReleaseCapability == 1u);
}

TEST_CASE(
    "Production reviewed process-image resolver remains closed while source registry is empty",
    "[quest.party-state][native-load-resolver][process-image][registry]")
{
    ResetFake();
    TrustedFixture fixture;
    Owner owner;

    const auto rejected =
        PartyQuestSkyrimNativeLoadBridgeResolver::
            ResolveReviewedProcessImageAndBind(
                fixture.Runtime,
                fixture.Generation,
                owner);

    REQUIRE(rejected.Status == BindStatus::ResolveRejected);
    REQUIRE_FALSE(rejected.IsBound());
    REQUIRE(
        rejected.ResolverStatus ==
        ResolveStatus::SourceAuthorizationRejected);
    REQUIRE(owner.Snapshot().Phase == OwnerPhase::Unbound);
}

TEST_CASE(
    "Native load resolver hands authenticated lease directly to owner",
    "[quest.party-state][native-load-resolver][owner-handoff]")
{
    ResetFake();
    TrustedFixture fixture;
    Owner owner;

    const auto bound =
        PartyQuestSkyrimNativeLoadBridgeResolver::ResolveAndBind(
            fixture.TrustedDirectory,
            fixture.Runtime,
            fixture.Source,
            fixture.Generation,
            owner);

    REQUIRE(bound.Status == BindStatus::Bound);
    REQUIRE(bound.IsBound());
    REQUIRE(bound.ResolverStatus == ResolveStatus::Resolved);
    REQUIRE(bound.LeaseStatus ==
        PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus::Ready);
    REQUIRE(bound.Owner.Status == OwnerStatus::Applied);
    REQUIRE(bound.Owner.State.Code == OwnerStateCode::Bound);
    REQUIRE(owner.Snapshot().Phase == OwnerPhase::Bound);
    REQUIRE(owner.Snapshot().BoundGeneration == fixture.Generation);
    REQUIRE(owner.Snapshot().RuntimeFingerprint ==
        kRuntimeFingerprint);

    const auto shutdown = owner.Shutdown();
    REQUIRE(shutdown.Status == OwnerStatus::Applied);
    REQUIRE(shutdown.State.Code == OwnerStateCode::ShutdownComplete);
    REQUIRE(shutdown.State.ReleaseCapability == 1u);
}

TEST_CASE(
    "Production reviewed resolver path remains closed while source registry is empty",
    "[quest.party-state][native-load-resolver][owner-handoff][registry]")
{
    ResetFake();
    TrustedFixture fixture;
    Owner owner;

    const auto rejected =
        PartyQuestSkyrimNativeLoadBridgeResolver::ResolveReviewedAndBind(
            fixture.TrustedDirectory,
            fixture.Runtime,
            fixture.Generation,
            owner);

    REQUIRE(rejected.Status == BindStatus::ResolveRejected);
    REQUIRE_FALSE(rejected.IsBound());
    REQUIRE(
        rejected.ResolverStatus ==
        ResolveStatus::SourceAuthorizationRejected);
    REQUIRE(owner.Snapshot().Phase == OwnerPhase::Unbound);
    REQUIRE(owner.Snapshot().CapabilityRetained == 0u);
}

TEST_CASE(
    "Resolved pinned lease cannot cross a generation transition before owner bind",
    "[quest.party-state][native-load-resolver][owner-handoff][generation]")
{
    ResetFake();
    TrustedFixture fixture;
    Owner owner;

    auto resolved =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::ResolveAndPin(
            fixture.TrustedDirectory,
            fixture.Runtime,
            fixture.Source,
            fixture.Generation);
    REQUIRE(resolved.IsResolved());
    REQUIRE(resolved.Lease);
    REQUIRE(resolved.Lease->IsPinned());

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const auto ticket = fence.BeginLifecycleTransition();
    REQUIRE(ticket.IsValid());
    REQUIRE(ticket.Generation != fixture.Generation);
    REQUIRE(fence.CompleteLifecycleTransition(ticket));

    const auto rejected =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::BindResolved(
            owner,
            std::move(resolved));

    REQUIRE(rejected.Status == BindStatus::OwnerRejected);
    REQUIRE_FALSE(rejected.IsBound());
    REQUIRE(rejected.ResolverStatus == ResolveStatus::Resolved);
    REQUIRE(rejected.Owner.Status == OwnerStatus::GenerationUnavailable);
    REQUIRE(owner.Snapshot().Phase == OwnerPhase::Unbound);
    REQUIRE(owner.Snapshot().CapabilityRetained == 0u);
}

TEST_CASE(
    "Native load process-image resolver cannot bind while generation transition is pending",
    "[quest.party-state][native-load-resolver][process-image][generation]")
{
    ResetFake();
    TrustedFixture fixture;
    const auto source = MakeProcessImageSource(fixture);

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const auto ticket = fence.BeginLifecycleTransition();
    REQUIRE(ticket.IsValid());

    const auto result =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::
            ResolveProcessImageAndPin(
                fixture.Runtime,
                source,
                ticket.Generation);
    REQUIRE(result.Status == ResolveStatus::GenerationUnavailable);
    REQUIRE_FALSE(result.IsResolved());

    REQUIRE(fence.CompleteLifecycleTransition(ticket));
}

TEST_CASE(
    "Native load bridge resolver cannot bind while process generation transition is pending",
    "[quest.party-state][native-load-resolver][generation]")
{
    ResetFake();
    TrustedFixture fixture;

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const auto ticket = fence.BeginLifecycleTransition();
    REQUIRE(ticket.IsValid());

    // Resolve against the exact newly published generation. TryAcquire still
    // fails while the asynchronous lifecycle ticket is pending.
    const auto result =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::ResolveAndPin(
            fixture.TrustedDirectory,
            fixture.Runtime,
            fixture.Source,
            ticket.Generation);
    REQUIRE(result.Status == ResolveStatus::GenerationUnavailable);
    REQUIRE_FALSE(result.IsResolved());

    REQUIRE(fence.CompleteLifecycleTransition(ticket));
}

TEST_CASE(
    "Native load bridge resolver rejects stale expected generation after transition completes",
    "[quest.party-state][native-load-resolver][generation][stale]")
{
    ResetFake();
    TrustedFixture fixture;

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const uint64_t oldGeneration = fixture.Generation;
    const auto ticket = fence.BeginLifecycleTransition();
    REQUIRE(ticket.IsValid());
    REQUIRE(ticket.Generation != oldGeneration);
    REQUIRE(fence.CompleteLifecycleTransition(ticket));

    const auto result =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::ResolveAndPin(
            fixture.TrustedDirectory,
            fixture.Runtime,
            fixture.Source,
            oldGeneration);
    REQUIRE(result.Status == ResolveStatus::GenerationUnavailable);
    REQUIRE_FALSE(result.IsResolved());
}
#endif
