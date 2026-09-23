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
using SourceAuthorization =
    PartyQuestSkyrimNativeLoadBridgeSourceAuthorization;
using RuntimeAuthorization =
    PartyQuestSkyrimRuntimeIdentityAuthorization;
using RuntimeVersion = PartyQuestSkyrimRuntimeVersion;
using ExecutableIdentity = PartyQuestSkyrimExecutableIdentity;
using DescriptorResult = PartyQuestNativeLoadBridgeDescriptorResult;
using BridgeStatus = PartyQuestNativeLoadBridgeStatus;

constexpr RuntimeVersion kRuntime{1u, 6u, 1170u, 0u};
constexpr uint64_t kRuntimeFingerprint = 0x4C4F414452455331ull;

enum class FakeDescriptorMode : uint8_t
{
    Valid,
    Unavailable,
    BadFingerprint,
    ThrowCpp,
    RaiseSeh
};

FakeDescriptorMode g_descriptorMode{FakeDescriptorMode::Valid};

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

ExecutableIdentity MakeExecutableIdentity(uint8_t aSeed) noexcept
{
    ExecutableIdentity identity{};
    identity.Sha256[0] = aSeed == 0u ? 1u : aSeed;
    identity.Sha256[31] =
        static_cast<uint8_t>(identity.Sha256[0] ^ 0xA5u);
    return identity;
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
}
} // namespace

extern "C" __declspec(dllexport) uint32_t
PartyQuestSKSE_GetLoadBridgeDescriptor(
    PartyQuestNativeLoadBridgeDescriptorV1* apDescriptor,
    uint32_t aDescriptorSize)
{
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

    static bool HashFile(
        const std::filesystem::path& acPath,
        std::array<uint8_t, 32>& aHash) noexcept
    {
        return PartyQuestSkyrimNativeLoadBridgeResolver::HashFileSha256(
            acPath,
            aHash);
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
        , SkyrimExecutableIdentity(MakeExecutableIdentity(0x42u))
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
    return PartyQuestSkyrimNativeLoadBridgeResolver::ResolveAndPin(
        acFixture.TrustedDirectory,
        acFixture.Runtime,
        acFixture.Source,
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
    std::array<uint8_t, 32> hash{};
    hash[0] = 1u;

    const auto unreviewed =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::
            MakeSourceAuthorization(
                kRuntime,
                executableIdentity,
                L"native-load-test.dll",
                hash,
                kRuntimeFingerprint,
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
                kRuntimeFingerprint);
    REQUIRE_FALSE(parentTraversal.IsVerified());

    const auto currentTraversal =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::
            MakeSourceAuthorization(
                kRuntime,
                executableIdentity,
                std::filesystem::path(L".") /
                    L"native-load-test.dll",
                hash,
                kRuntimeFingerprint);
    REQUIRE_FALSE(currentTraversal.IsVerified());

    const auto absolute =
        PartyQuestSkyrimNativeLoadBridgeResolverTestAccess::
            MakeSourceAuthorization(
                kRuntime,
                executableIdentity,
                std::filesystem::path(L"C:\\native-load-test.dll"),
                hash,
                kRuntimeFingerprint);
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
                kRuntimeFingerprint);
    REQUIRE_FALSE(noHash.IsVerified());

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
    "Native load bridge resolver rejects invalid trust inputs before module use",
    "[quest.party-state][native-load-resolver][trust-boundary]")
{
    ResetFake();
    TrustedFixture fixture;

    SECTION("relative trusted root")
    {
        const auto result =
            PartyQuestSkyrimNativeLoadBridgeResolver::ResolveAndPin(
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
            PartyQuestSkyrimNativeLoadBridgeResolver::ResolveAndPin(
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
            PartyQuestSkyrimNativeLoadBridgeResolver::ResolveAndPin(
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
            PartyQuestSkyrimNativeLoadBridgeResolver::ResolveAndPin(
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
            PartyQuestSkyrimNativeLoadBridgeResolver::ResolveAndPin(
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
            PartyQuestSkyrimNativeLoadBridgeResolver::ResolveAndPin(
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
        PartyQuestSkyrimNativeLoadBridgeResolver::ResolveAndPin(
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
        PartyQuestSkyrimNativeLoadBridgeResolver::ResolveAndPin(
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
        PartyQuestSkyrimNativeLoadBridgeResolver::ResolveAndPin(
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
        PartyQuestSkyrimNativeLoadBridgeResolver::ResolveAndPin(
            fixture.TrustedDirectory,
            fixture.Runtime,
            fixture.Source,
            oldGeneration);
    REQUIRE(result.Status == ResolveStatus::GenerationUnavailable);
    REQUIRE_FALSE(result.IsResolved());
}
#endif
