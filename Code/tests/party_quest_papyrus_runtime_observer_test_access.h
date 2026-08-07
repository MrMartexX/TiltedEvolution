#pragma once

#include <Structs/Skyrim/PartyQuestPapyrusRuntimeMonitor.h>

/**
 * Test-only issuer for observer/runtime-profile trust. Production code cannot
 * obtain a live observer authorization from the public observer interface or
 * from caller-supplied profile metadata alone.
 */
class PartyQuestPapyrusRuntimeObserverTestAccess final
{
public:
    static constexpr uint64_t kVerifiedTestRuntimeProfileFingerprint =
        0x5051525450524F46ull;

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
        const PartyQuestPapyrusRuntimeProfileAuthorization runtimeProfile(
            aRuntimeProfileFingerprint,
            aExactRuntimeMatch,
            aObservedWorkDomains,
            aCoherentSnapshot,
            aTrustedQuestEventGeneration);
        return PartyQuestPapyrusRuntimeObserverAuthorization(
            acObserver,
            runtimeProfile);
    }
};
