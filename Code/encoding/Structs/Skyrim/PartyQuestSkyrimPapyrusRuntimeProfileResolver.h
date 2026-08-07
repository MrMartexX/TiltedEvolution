#pragma once

#include <Structs/Skyrim/PartyQuestPapyrusRuntimeMonitor.h>

#include <cstdint>

class PartyQuestSkyrimRuntimeIdentityResolver;
class PartyQuestPapyrusRuntimeObserverTestAccess;

/**
 * Exact executable-version identity carried by the trusted Skyrim startup
 * boundary. This is data only; possessing the numeric tuple is not authority.
 */
struct PartyQuestSkyrimRuntimeVersion final
{
    uint32_t Major{};
    uint32_t Minor{};
    uint32_t Patch{};
    uint32_t Build{};

    [[nodiscard]] bool Matches(
        const PartyQuestSkyrimRuntimeVersion& acOther) const noexcept
    {
        return Major == acOther.Major &&
            Minor == acOther.Minor &&
            Patch == acOther.Patch &&
            Build == acOther.Build;
    }
};

/**
 * Process-local proof that an exact SkyrimSE executable identity was obtained
 * through the trusted startup/runtime-version boundary and independently
 * accepted by the project's VersionDb support gate.
 *
 * Callers cannot mint this capability from a version string or numeric tuple.
 * A future client-specific PartyQuestSkyrimRuntimeIdentityResolver is the only
 * production issuer; it must bind to the already loaded executable and
 * successful VersionDb validation rather than accepting remote/caller supplied
 * paths or version metadata.
 */
class PartyQuestSkyrimRuntimeIdentityAuthorization final
{
public:
    PartyQuestSkyrimRuntimeIdentityAuthorization() noexcept = default;

    [[nodiscard]] bool IsVerified() const noexcept
    {
        return m_exactSkyrimSeExecutable &&
            m_versionDbSupported &&
            m_runtimeVersion.Major != 0;
    }

    [[nodiscard]] const PartyQuestSkyrimRuntimeVersion& GetRuntimeVersion() const noexcept
    {
        return m_runtimeVersion;
    }

private:
    friend class PartyQuestSkyrimRuntimeIdentityResolver;
    friend class PartyQuestSkyrimPapyrusRuntimeProfileResolver;
    friend class PartyQuestPapyrusRuntimeObserverTestAccess;

    explicit PartyQuestSkyrimRuntimeIdentityAuthorization(
        PartyQuestSkyrimRuntimeVersion aRuntimeVersion,
        bool aExactSkyrimSeExecutable,
        bool aVersionDbSupported) noexcept
        : m_runtimeVersion(aRuntimeVersion)
        , m_exactSkyrimSeExecutable(aExactSkyrimSeExecutable)
        , m_versionDbSupported(aVersionDbSupported)
    {
    }

    PartyQuestSkyrimRuntimeVersion m_runtimeVersion{};
    bool m_exactSkyrimSeExecutable{};
    bool m_versionDbSupported{};
};

/**
 * Fail-closed bridge from trusted executable identity to the much stronger
 * Papyrus VM observation-profile capability.
 *
 * Address-Library/VersionDb support alone is intentionally insufficient: every
 * production profile must additionally have an exact executable-version match,
 * a proven complete six-domain Papyrus mapping, coherent snapshot semantics and
 * a trusted monotonic quest-event generation source.
 *
 * No production Skyrim runtime profile is approved yet. Resolve() therefore
 * returns an invalid capability for every runtime until an ABI/layout profile
 * is supported by separate evidence. This preserves the invariant that an
 * unknown or merely VersionDb-supported executable causes zero authoritative
 * VM sampling.
 */
class PartyQuestSkyrimPapyrusRuntimeProfileResolver final
{
public:
    [[nodiscard]] static PartyQuestPapyrusRuntimeProfileAuthorization Resolve(
        const PartyQuestSkyrimRuntimeIdentityAuthorization& acRuntimeIdentity) noexcept
    {
        if (!acRuntimeIdentity.IsVerified())
            return {};

        // Intentionally empty production registry. Do not add an entry from a
        // version string, Address Library support, or guessed VM offsets alone.
        return {};
    }

private:
    friend class PartyQuestPapyrusRuntimeObserverTestAccess;

    struct ProfileDescriptor final
    {
        PartyQuestSkyrimRuntimeVersion RuntimeVersion{};
        uint64_t RuntimeProfileFingerprint{};
        uint32_t ObservedWorkDomains{};
        bool CoherentSnapshot{};
        bool TrustedQuestEventGeneration{};
    };

    /**
     * Shared exact-match primitive for future audited registry entries. Kept
     * private so production callers cannot inject profile metadata. Tests reach
     * it only through the named test-access friend.
     */
    [[nodiscard]] static PartyQuestPapyrusRuntimeProfileAuthorization ResolveExactProfile(
        const PartyQuestSkyrimRuntimeIdentityAuthorization& acRuntimeIdentity,
        const ProfileDescriptor& acProfile) noexcept
    {
        if (!acRuntimeIdentity.IsVerified() ||
            !acRuntimeIdentity.GetRuntimeVersion().Matches(acProfile.RuntimeVersion))
        {
            return {};
        }

        return PartyQuestPapyrusRuntimeProfileAuthorization(
            acProfile.RuntimeProfileFingerprint,
            true,
            acProfile.ObservedWorkDomains,
            acProfile.CoherentSnapshot,
            acProfile.TrustedQuestEventGeneration);
    }
};
