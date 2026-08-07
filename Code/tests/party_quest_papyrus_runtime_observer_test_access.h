#pragma once

#include <Structs/Skyrim/PartyQuestSkyrimPapyrusRuntimeProfileResolver.h>

/**
 * Test-only issuer for observer/runtime-profile trust. Production code cannot
 * obtain a live observer authorization from the public observer interface or
 * from caller-supplied profile/generation metadata alone.
 */
class PartyQuestPapyrusRuntimeObserverTestAccess final
{
public:
    static constexpr uint64_t kVerifiedTestRuntimeProfileFingerprint =
        0x5051525450524F46ull;
    static constexpr uint64_t kVerifiedTestGenerationSourceFingerprint =
        0x505147454E535243ull;

    [[nodiscard]] static PartyQuestSkyrimRuntimeIdentityAuthorization
    AuthorizeRuntimeIdentity(
        uint32_t aMajor,
        uint32_t aMinor,
        uint32_t aPatch,
        uint32_t aBuild,
        bool aExactSkyrimSeExecutable = true,
        bool aVersionDbSupported = true) noexcept
    {
        return PartyQuestSkyrimRuntimeIdentityAuthorization(
            {aMajor, aMinor, aPatch, aBuild},
            aExactSkyrimSeExecutable,
            aVersionDbSupported);
    }

    [[nodiscard]] static PartyQuestPapyrusRuntimeGenerationAuthorization
    AuthorizeGenerationSource(
        uint64_t aSourceFingerprint = kVerifiedTestGenerationSourceFingerprint,
        uint32_t aCoveredWorkDomains = kPartyQuestPapyrusRuntimeRequiredWorkDomains,
        bool aMonotonic = true,
        bool aObservesWorkArrival = true) noexcept
    {
        return PartyQuestPapyrusRuntimeGenerationAuthorization(
            aSourceFingerprint,
            aCoveredWorkDomains,
            aMonotonic,
            aObservesWorkArrival);
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
            aTrustedQuestEventGeneration);
        return ResolveRuntimeProfileWithGenerationForTesting(
            acRuntimeIdentity,
            generation,
            aProfileMajor,
            aProfileMinor,
            aProfilePatch,
            aProfileBuild,
            aRuntimeProfileFingerprint,
            aObservedWorkDomains,
            aCoherentSnapshot);
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
        const PartyQuestSkyrimPapyrusRuntimeProfileResolver::ProfileDescriptor profile{
            {aProfileMajor, aProfileMinor, aProfilePatch, aProfileBuild},
            aRuntimeProfileFingerprint,
            aObservedWorkDomains,
            aCoherentSnapshot};
        return PartyQuestSkyrimPapyrusRuntimeProfileResolver::ResolveExactProfile(
            acRuntimeIdentity,
            acGeneration,
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
            aTrustedQuestEventGeneration);
        const PartyQuestPapyrusRuntimeProfileAuthorization runtimeProfile(
            aRuntimeProfileFingerprint,
            aExactRuntimeMatch,
            aObservedWorkDomains,
            aCoherentSnapshot,
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