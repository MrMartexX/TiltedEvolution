#pragma once

#include <Structs/Skyrim/PartyQuestPapyrusRuntimeMonitor.h>

#include <cstdint>
#include <limits>
#include <string_view>

class PartyQuestSkyrimPapyrusGenerationSourceResolver;
class PartyQuestSkyrimPapyrusSnapshotResolver;
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

    /**
     * Strictly parses the canonical four-component VersionDb representation.
     * This produces data only and never grants runtime authority. The output is
     * unchanged on failure so malformed/overflowing version text cannot become
     * a partially accepted identity.
     */
    [[nodiscard]] static bool TryParse(
        std::string_view aVersion,
        PartyQuestSkyrimRuntimeVersion& aOut) noexcept
    {
        uint32_t components[4]{};
        size_t position = 0;

        for (size_t componentIndex = 0; componentIndex < 4; ++componentIndex)
        {
            if (position >= aVersion.size() ||
                aVersion[position] < '0' || aVersion[position] > '9')
            {
                return false;
            }

            uint64_t value = 0;
            while (position < aVersion.size() &&
                aVersion[position] >= '0' && aVersion[position] <= '9')
            {
                const uint32_t digit =
                    static_cast<uint32_t>(aVersion[position] - '0');
                if (value >
                    (std::numeric_limits<uint32_t>::max() - digit) / 10ull)
                {
                    return false;
                }

                value = value * 10ull + digit;
                ++position;
            }

            components[componentIndex] = static_cast<uint32_t>(value);

            if (componentIndex < 3)
            {
                if (position >= aVersion.size() || aVersion[position] != '.')
                    return false;
                ++position;
            }
            else if (position != aVersion.size())
            {
                return false;
            }
        }

        if (components[0] == 0)
            return false;

        aOut = {components[0], components[1], components[2], components[3]};
        return true;
    }
};

/**
 * Process-local proof that an exact SkyrimSE executable identity was obtained
 * through the trusted startup/runtime-version boundary and independently
 * accepted by the project's VersionDb support gate.
 *
 * Callers cannot mint this capability from a version string or numeric tuple.
 * PartyQuestSkyrimRuntimeIdentityResolver is the only production issuer.
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
 * Production bridge from the launcher's actual mapped Skyrim executable to the
 * process-local runtime identity capability.
 *
 * Resolve() accepts no path/version arguments. The Windows launcher
 * implementation reads its long-lived LaunchContext, whose version was queried
 * from the same executable path whose bytes ExeLoader successfully mapped, and
 * independently compares that exact tuple with the VersionDb instance that
 * successfully loaded before client initialization. Any absent state, malformed
 * version, failed VersionDb load or tuple mismatch fails closed.
 *
 * This proves an exact executable-version contract at the existing startup
 * trust boundary; it is not a cryptographic executable authenticity proof.
 */
class PartyQuestSkyrimRuntimeIdentityResolver final
{
public:
    [[nodiscard]] static PartyQuestSkyrimRuntimeIdentityAuthorization Resolve() noexcept;

private:
    friend class PartyQuestPapyrusRuntimeObserverTestAccess;

    [[nodiscard]] static PartyQuestSkyrimRuntimeIdentityAuthorization ResolveTrustedState(
        const PartyQuestSkyrimRuntimeVersion& acMappedExecutableVersion,
        bool aMappedExecutableLoaded,
        const PartyQuestSkyrimRuntimeVersion& acVersionDbVersion,
        bool aVersionDbLoaded) noexcept
    {
        if (!aMappedExecutableLoaded ||
            !aVersionDbLoaded ||
            acMappedExecutableVersion.Major == 0 ||
            !acMappedExecutableVersion.Matches(acVersionDbVersion))
        {
            return {};
        }

        return PartyQuestSkyrimRuntimeIdentityAuthorization(
            acMappedExecutableVersion,
            true,
            true);
    }
};

/**
 * Process-local proof that the generation accompanying authoritative Papyrus
 * observations comes from a source that covers the complete required VM work
 * envelope and is monotonic across the observer lifetime.
 *
 * A numeric generation value, event dispatcher, hook address or source
 * fingerprint is not authority by itself. Production code may receive this
 * capability only from the concrete Skyrim generation-source resolver after
 * the hook/source coverage contract has been proven for the exact runtime.
 * Until such a resolver exists, production cannot satisfy this prerequisite.
 */
class PartyQuestPapyrusRuntimeGenerationAuthorization final
{
public:
    PartyQuestPapyrusRuntimeGenerationAuthorization() noexcept = default;

    [[nodiscard]] bool IsVerified() const noexcept
    {
        return m_sourceFingerprint != 0 &&
            HasCompletePartyQuestPapyrusRuntimeWorkEnvelope(m_coveredWorkDomains) &&
            m_monotonic &&
            m_observesWorkArrival;
    }

    [[nodiscard]] uint64_t GetSourceFingerprint() const noexcept
    {
        return m_sourceFingerprint;
    }

    [[nodiscard]] uint32_t GetCoveredWorkDomains() const noexcept
    {
        return m_coveredWorkDomains;
    }

private:
    friend class PartyQuestSkyrimPapyrusGenerationSourceResolver;
    friend class PartyQuestSkyrimPapyrusRuntimeProfileResolver;
    friend class PartyQuestPapyrusRuntimeObserverTestAccess;

    explicit PartyQuestPapyrusRuntimeGenerationAuthorization(
        uint64_t aSourceFingerprint,
        uint32_t aCoveredWorkDomains,
        bool aMonotonic,
        bool aObservesWorkArrival) noexcept
        : m_sourceFingerprint(aSourceFingerprint)
        , m_coveredWorkDomains(aCoveredWorkDomains)
        , m_monotonic(aMonotonic)
        , m_observesWorkArrival(aObservesWorkArrival)
    {
    }

    uint64_t m_sourceFingerprint{};
    uint32_t m_coveredWorkDomains{};
    bool m_monotonic{};
    bool m_observesWorkArrival{};
};

/**
 * Process-local proof that one authoritative Papyrus observation can obtain a
 * coherent, read-only snapshot of the complete required VM work envelope.
 *
 * Container offsets, nearby lock fields, game-thread execution or a boolean
 * claim of coherence are not authority by themselves. A future concrete Skyrim
 * snapshot resolver may issue this capability only after the exact runtime's
 * layout, lock ownership/order and failure behavior have been verified.
 * Sampling must fail closed rather than silently falling back to unlocked,
 * partial or best-effort reads.
 */
class PartyQuestPapyrusRuntimeSnapshotAuthorization final
{
public:
    PartyQuestPapyrusRuntimeSnapshotAuthorization() noexcept = default;

    [[nodiscard]] bool IsVerified() const noexcept
    {
        return m_snapshotFingerprint != 0 &&
            HasCompletePartyQuestPapyrusRuntimeWorkEnvelope(m_coveredWorkDomains) &&
            m_readOnly &&
            m_crossDomainCoherent &&
            m_failClosedOnSamplingFailure;
    }

    [[nodiscard]] uint64_t GetSnapshotFingerprint() const noexcept
    {
        return m_snapshotFingerprint;
    }

    [[nodiscard]] uint32_t GetCoveredWorkDomains() const noexcept
    {
        return m_coveredWorkDomains;
    }

private:
    friend class PartyQuestSkyrimPapyrusSnapshotResolver;
    friend class PartyQuestSkyrimPapyrusRuntimeProfileResolver;
    friend class PartyQuestPapyrusRuntimeObserverTestAccess;

    explicit PartyQuestPapyrusRuntimeSnapshotAuthorization(
        uint64_t aSnapshotFingerprint,
        uint32_t aCoveredWorkDomains,
        bool aReadOnly,
        bool aCrossDomainCoherent,
        bool aFailClosedOnSamplingFailure) noexcept
        : m_snapshotFingerprint(aSnapshotFingerprint)
        , m_coveredWorkDomains(aCoveredWorkDomains)
        , m_readOnly(aReadOnly)
        , m_crossDomainCoherent(aCrossDomainCoherent)
        , m_failClosedOnSamplingFailure(aFailClosedOnSamplingFailure)
    {
    }

    uint64_t m_snapshotFingerprint{};
    uint32_t m_coveredWorkDomains{};
    bool m_readOnly{};
    bool m_crossDomainCoherent{};
    bool m_failClosedOnSamplingFailure{};
};

/**
 * Fail-closed bridge from trusted executable identity to the much stronger
 * Papyrus VM observation-profile capability.
 *
 * Address-Library/VersionDb support alone is intentionally insufficient: every
 * production profile must additionally have an exact executable-version match,
 * a proven complete six-domain Papyrus mapping, a separately authorized
 * coherent read-only snapshot contract and a separately authorized monotonic
 * work-generation source.
 *
 * No production Skyrim runtime profile is approved yet. Resolve() therefore
 * returns an invalid capability for every runtime until the ABI/layout,
 * snapshot and generation contracts are supported by separate evidence. This
 * preserves the invariant that an unknown or merely VersionDb-supported
 * executable causes zero authoritative VM sampling.
 */
class PartyQuestSkyrimPapyrusRuntimeProfileResolver final
{
public:
    [[nodiscard]] static PartyQuestPapyrusRuntimeProfileAuthorization Resolve(
        const PartyQuestSkyrimRuntimeIdentityAuthorization& acRuntimeIdentity,
        const PartyQuestPapyrusRuntimeGenerationAuthorization& acGeneration,
        const PartyQuestPapyrusRuntimeSnapshotAuthorization& acSnapshot) noexcept
    {
        if (!acRuntimeIdentity.IsVerified() ||
            !acGeneration.IsVerified() ||
            !acSnapshot.IsVerified())
        {
            return {};
        }

        // Intentionally empty production registry. Do not add an entry from a
        // version string, Address Library support, guessed VM offsets, unlocked
        // container reads or an unproven generation hook alone. Any future
        // entry must bind exact runtime, generation-source and snapshot contract
        // identities; fingerprints identify audited contracts, not trust them.
        return {};
    }

private:
    friend class PartyQuestPapyrusRuntimeObserverTestAccess;

    struct ProfileDescriptor final
    {
        PartyQuestSkyrimRuntimeVersion RuntimeVersion{};
        uint64_t RuntimeProfileFingerprint{};
        uint64_t GenerationSourceFingerprint{};
        uint64_t SnapshotFingerprint{};
        uint32_t ObservedWorkDomains{};
    };

    /**
     * Shared exact-match primitive for future audited registry entries. Kept
     * private so production callers cannot inject profile metadata. Tests reach
     * it only through the named test-access friend. A valid generic capability
     * is insufficient: its deterministic contract identity must match the exact
     * generation/snapshot contract audited for this runtime profile.
     */
    [[nodiscard]] static PartyQuestPapyrusRuntimeProfileAuthorization ResolveExactProfile(
        const PartyQuestSkyrimRuntimeIdentityAuthorization& acRuntimeIdentity,
        const PartyQuestPapyrusRuntimeGenerationAuthorization& acGeneration,
        const PartyQuestPapyrusRuntimeSnapshotAuthorization& acSnapshot,
        const ProfileDescriptor& acProfile) noexcept
    {
        if (!acRuntimeIdentity.IsVerified() ||
            !acGeneration.IsVerified() ||
            !acSnapshot.IsVerified() ||
            !acRuntimeIdentity.GetRuntimeVersion().Matches(acProfile.RuntimeVersion) ||
            acGeneration.GetSourceFingerprint() != acProfile.GenerationSourceFingerprint ||
            acSnapshot.GetSnapshotFingerprint() != acProfile.SnapshotFingerprint ||
            acGeneration.GetCoveredWorkDomains() != acProfile.ObservedWorkDomains ||
            acSnapshot.GetCoveredWorkDomains() != acProfile.ObservedWorkDomains)
        {
            return {};
        }

        return PartyQuestPapyrusRuntimeProfileAuthorization(
            acProfile.RuntimeProfileFingerprint,
            true,
            acProfile.ObservedWorkDomains,
            true,
            acGeneration.GetSourceFingerprint());
    }
};
