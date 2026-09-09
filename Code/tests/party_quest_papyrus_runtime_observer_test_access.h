#pragma once

#include <Structs/Skyrim/PartyQuestSkyrimPapyrusRuntimeProfileResolver.h>

/**
 * Test-only issuer for observer/runtime-profile trust. Production code cannot
 * obtain these capabilities from caller-supplied metadata.
 */
class PartyQuestPapyrusRuntimeObserverTestAccess final
{
public:
    [[nodiscard]] static constexpr PartyQuestSkyrimExecutableIdentity
    VerifiedTestExecutableIdentity() noexcept
    {
        PartyQuestSkyrimExecutableIdentity identity{};
        identity.Sha256[0] = 0xA5u;
        return identity;
    }

    static constexpr uint64_t kVerifiedTestRuntimeProfileFingerprint =
        0x5051525450524F46ull;
    static constexpr uint64_t kVerifiedTestGenerationSourceFingerprint =
        0x505147454E535243ull;
    static constexpr uint64_t kVerifiedTestSnapshotFingerprint =
        0x5051534E41505348ull;
    static constexpr uint32_t kVerifiedTestRuntimeMajor = 9;
    static constexpr uint32_t kVerifiedTestRuntimeMinor = 9;
    static constexpr uint32_t kVerifiedTestRuntimePatch = 9001;
    static constexpr uint32_t kVerifiedTestRuntimeBuild = 42;

    [[nodiscard]] static PartyQuestSkyrimRuntimeIdentityAuthorization
    AuthorizeRuntimeIdentity(
        uint32_t aMajor,
        uint32_t aMinor,
        uint32_t aPatch,
        uint32_t aBuild,
        bool aExactSkyrimSeExecutable = true,
        bool aVersionDbSupported = true,
        uint8_t aExecutableIdentityTag = 0xA5u) noexcept
    {
        auto executableIdentity = VerifiedTestExecutableIdentity();
        executableIdentity.Sha256[0] = aExecutableIdentityTag;
        return PartyQuestSkyrimRuntimeIdentityAuthorization(
            {aMajor, aMinor, aPatch, aBuild},
            aExactSkyrimSeExecutable,
            aVersionDbSupported,
            executableIdentity);
    }

    [[nodiscard]] static PartyQuestSkyrimRuntimeIdentityAuthorization
    AuthorizeInstalledRuntime161170Identity() noexcept
    {
        return PartyQuestSkyrimRuntimeIdentityAuthorization(
            {1, 6, 1170, 0},
            true,
            true,
            {{
                0xC4, 0x34, 0x20, 0x88, 0x94, 0xF0, 0x7F, 0x60,
                0x4B, 0x85, 0x2F, 0x29, 0xB8, 0xED, 0xC3, 0xA5,
                0x8C, 0x4D, 0xE6, 0x3D, 0xE7, 0x83, 0x37, 0x37,
                0x33, 0xE7, 0x2B, 0x2B, 0x73, 0xF3, 0x3B, 0xE9}});
    }

    [[nodiscard]] static PartyQuestSkyrimRuntimeIdentityAuthorization
    ResolveRuntimeIdentityForTesting(
        uint32_t aMappedMajor,
        uint32_t aMappedMinor,
        uint32_t aMappedPatch,
        uint32_t aMappedBuild,
        bool aMappedExecutableLoaded,
        uint32_t aVersionDbMajor,
        uint32_t aVersionDbMinor,
        uint32_t aVersionDbPatch,
        uint32_t aVersionDbBuild,
        bool aVersionDbLoaded) noexcept
    {
        return PartyQuestSkyrimRuntimeIdentityResolver::ResolveTrustedState(
            {aMappedMajor, aMappedMinor, aMappedPatch, aMappedBuild},
            aMappedExecutableLoaded,
            {aVersionDbMajor, aVersionDbMinor, aVersionDbPatch, aVersionDbBuild},
            aVersionDbLoaded,
            VerifiedTestExecutableIdentity());
    }

    [[nodiscard]] static PartyQuestPapyrusRuntimeGenerationAuthorization
    AuthorizeGenerationSource(
        uint64_t aSourceFingerprint = kVerifiedTestGenerationSourceFingerprint,
        uint32_t aCoveredWorkDomains = kPartyQuestPapyrusRuntimeRequiredWorkDomains,
        bool aMonotonic = true,
        bool aObservesWorkArrival = true,
        bool aSampleIndependentArrivalEpoch = true,
        uint32_t aRuntimeMajor = kVerifiedTestRuntimeMajor,
        uint32_t aRuntimeMinor = kVerifiedTestRuntimeMinor,
        uint32_t aRuntimePatch = kVerifiedTestRuntimePatch,
        uint32_t aRuntimeBuild = kVerifiedTestRuntimeBuild) noexcept
    {
        return PartyQuestPapyrusRuntimeGenerationAuthorization(
            {aRuntimeMajor, aRuntimeMinor, aRuntimePatch, aRuntimeBuild},
            aSourceFingerprint,
            aCoveredWorkDomains,
            aMonotonic,
            aObservesWorkArrival,
            aSampleIndependentArrivalEpoch);
    }

    [[nodiscard]] static PartyQuestPapyrusRuntimeSnapshotAuthorization
    AuthorizeSnapshot(
        uint64_t aSnapshotFingerprint = kVerifiedTestSnapshotFingerprint,
        uint32_t aCoveredWorkDomains = kPartyQuestPapyrusRuntimeRequiredWorkDomains,
        bool aReadOnly = true,
        bool aCrossDomainCoherent = true,
        bool aFailClosedOnSamplingFailure = true,
        uint32_t aRuntimeMajor = kVerifiedTestRuntimeMajor,
        uint32_t aRuntimeMinor = kVerifiedTestRuntimeMinor,
        uint32_t aRuntimePatch = kVerifiedTestRuntimePatch,
        uint32_t aRuntimeBuild = kVerifiedTestRuntimeBuild) noexcept
    {
        return PartyQuestPapyrusRuntimeSnapshotAuthorization(
            {aRuntimeMajor, aRuntimeMinor, aRuntimePatch, aRuntimeBuild},
            aSnapshotFingerprint,
            aCoveredWorkDomains,
            aReadOnly,
            aCrossDomainCoherent,
            aFailClosedOnSamplingFailure);
    }

    [[nodiscard]] static PartyQuestPapyrusRuntimeProfileAuthorization
    ResolveRuntimeProfileForTesting(
        const PartyQuestSkyrimRuntimeIdentityAuthorization& acRuntimeIdentity,
        uint32_t aProfileMajor,
        uint32_t aProfileMinor,
        uint32_t aProfilePatch,
        uint32_t aProfileBuild,
        uint64_t aRuntimeProfileFingerprint,
        uint32_t aObservedWorkDomains,
        bool aCoherentSnapshot,
        bool aTrustedQuestEventGeneration) noexcept
    {
        const auto generation = AuthorizeGenerationSource(
            kVerifiedTestGenerationSourceFingerprint,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains,
            aTrustedQuestEventGeneration,
            aTrustedQuestEventGeneration,
            aTrustedQuestEventGeneration,
            aProfileMajor,
            aProfileMinor,
            aProfilePatch,
            aProfileBuild);
        const auto snapshot = AuthorizeSnapshot(
            kVerifiedTestSnapshotFingerprint,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains,
            aCoherentSnapshot,
            aCoherentSnapshot,
            aCoherentSnapshot,
            aProfileMajor,
            aProfileMinor,
            aProfilePatch,
            aProfileBuild);
        return ResolveRuntimeProfileWithEvidenceForTesting(
            acRuntimeIdentity,
            generation,
            snapshot,
            aProfileMajor,
            aProfileMinor,
            aProfilePatch,
            aProfileBuild,
            aRuntimeProfileFingerprint,
            aObservedWorkDomains);
    }

    [[nodiscard]] static PartyQuestPapyrusRuntimeProfileAuthorization
    ResolveRuntimeProfileWithGenerationForTesting(
        const PartyQuestSkyrimRuntimeIdentityAuthorization& acRuntimeIdentity,
        const PartyQuestPapyrusRuntimeGenerationAuthorization& acGeneration,
        uint32_t aProfileMajor,
        uint32_t aProfileMinor,
        uint32_t aProfilePatch,
        uint32_t aProfileBuild,
        uint64_t aRuntimeProfileFingerprint,
        uint32_t aObservedWorkDomains,
        bool aCoherentSnapshot) noexcept
    {
        const auto snapshot = AuthorizeSnapshot(
            kVerifiedTestSnapshotFingerprint,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains,
            aCoherentSnapshot,
            aCoherentSnapshot,
            aCoherentSnapshot,
            aProfileMajor,
            aProfileMinor,
            aProfilePatch,
            aProfileBuild);
        return ResolveRuntimeProfileWithEvidenceForTesting(
            acRuntimeIdentity,
            acGeneration,
            snapshot,
            aProfileMajor,
            aProfileMinor,
            aProfilePatch,
            aProfileBuild,
            aRuntimeProfileFingerprint,
            aObservedWorkDomains);
    }

    [[nodiscard]] static PartyQuestPapyrusRuntimeProfileAuthorization
    ResolveRuntimeProfileWithEvidenceForTesting(
        const PartyQuestSkyrimRuntimeIdentityAuthorization& acRuntimeIdentity,
        const PartyQuestPapyrusRuntimeGenerationAuthorization& acGeneration,
        const PartyQuestPapyrusRuntimeSnapshotAuthorization& acSnapshot,
        uint32_t aProfileMajor,
        uint32_t aProfileMinor,
        uint32_t aProfilePatch,
        uint32_t aProfileBuild,
        uint64_t aRuntimeProfileFingerprint,
        uint32_t aObservedWorkDomains,
        uint64_t aExpectedGenerationSourceFingerprint =
            kVerifiedTestGenerationSourceFingerprint,
        uint64_t aExpectedSnapshotFingerprint =
            kVerifiedTestSnapshotFingerprint) noexcept
    {
        const PartyQuestSkyrimPapyrusRuntimeProfileResolver::ProfileDescriptor profile{
            {aProfileMajor, aProfileMinor, aProfilePatch, aProfileBuild},
            VerifiedTestExecutableIdentity(),
            aRuntimeProfileFingerprint,
            aExpectedGenerationSourceFingerprint,
            aExpectedSnapshotFingerprint,
            aObservedWorkDomains};
        return PartyQuestSkyrimPapyrusRuntimeProfileResolver::ResolveExactProfile(
            acRuntimeIdentity,
            acGeneration,
            acSnapshot,
            profile);
    }

    [[nodiscard]] static PartyQuestPapyrusRuntimeObserverAuthorization Authorize(
        const PartyQuestPapyrusRuntimeObserver& acObserver) noexcept
    {
        return AuthorizeWithRuntimeProfile(
            acObserver,
            kVerifiedTestRuntimeProfileFingerprint,
            true,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains,
            true,
            true);
    }

    [[nodiscard]] static PartyQuestPapyrusRuntimeObserverAuthorization
    AuthorizeWithRuntimeProfile(
        const PartyQuestPapyrusRuntimeObserver& acObserver,
        uint64_t aRuntimeProfileFingerprint,
        bool aExactRuntimeMatch,
        uint32_t aObservedWorkDomains,
        bool aCoherentSnapshot,
        bool aTrustedQuestEventGeneration) noexcept
    {
        const auto generation = AuthorizeGenerationSource(
            kVerifiedTestGenerationSourceFingerprint,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains,
            aTrustedQuestEventGeneration,
            aTrustedQuestEventGeneration,
            aTrustedQuestEventGeneration);
        const auto snapshot = AuthorizeSnapshot(
            kVerifiedTestSnapshotFingerprint,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains,
            aCoherentSnapshot,
            aCoherentSnapshot,
            aCoherentSnapshot);
        const PartyQuestPapyrusRuntimeProfileAuthorization runtimeProfile(
            aRuntimeProfileFingerprint,
            aExactRuntimeMatch,
            aObservedWorkDomains,
            snapshot.IsVerified(),
            generation.IsVerified() ? generation.GetSourceFingerprint() : 0);
        return AuthorizeWithRuntimeProfileAuthorization(acObserver, runtimeProfile);
    }

    [[nodiscard]] static PartyQuestPapyrusRuntimeObserverAuthorization
    AuthorizeWithRuntimeProfileAuthorization(
        const PartyQuestPapyrusRuntimeObserver& acObserver,
        const PartyQuestPapyrusRuntimeProfileAuthorization& acRuntimeProfile) noexcept
    {
        return PartyQuestPapyrusRuntimeObserverAuthorization(
            acObserver,
            acRuntimeProfile);
    }
};
