#pragma once

#include <Structs/Skyrim/PartyQuestCompatibilityEnvironmentCache.h>
#include <Structs/Skyrim/PartyQuestRuntimeCompatibility.h>
#include <Structs/Skyrim/PartyQuestRuntimeProcessRequestGate.h>

#include <optional>

struct ModSystem;
struct TESQuest;

/**
 * Skyrim-specific production evidence boundary for the equal-party runtime
 * planner.
 *
 * The reviewed compatibility manifest is source-owned. A server delivery or a
 * local live observation can never mint a requirement by copying the values it
 * just observed. Until an exact quest profile is reviewed into the registry the
 * manifest is intentionally empty and production planning therefore stops at
 * RequirementUnavailable.
 *
 * When a profile exists, ObserveFresh() fingerprints the resolved live TESQuest
 * topology and combines it with process-owned environment fingerprints computed
 * once by PartyQuestCompatibilityEnvironmentCache. These
 * fingerprints are deterministic compatibility identities, not authentication
 * primitives. Any unavailable file, malformed runtime object or unresolved
 * GameId fails closed.
 */
class PartyQuestSkyrimRuntimeCompatibilityEvidence final
{
public:
    static constexpr uint32_t ProfileVersion = 1;

    // Reviewed identity of the current narrow QuestSnapshot-only native adapter
    // contract. This is deterministic contract identity, not a secret.
    static constexpr uint64_t NativeAdapterFingerprint =
        0x5354525153544147ull; // "STRQSTAG"

    [[nodiscard]] static PartyQuestRuntimeCompatibilityManifest
    BuildReviewedManifest() noexcept;

    [[nodiscard]] static bool HasReviewedProfile(const GameId& acQuestId) noexcept;

    /** Capture paths/order only; the caller may hash this snapshot off-thread. */
    [[nodiscard]] static std::optional<PartyQuestCompatibilityEnvironmentSnapshot>
    CaptureEnvironmentSnapshot() noexcept;

    /**
     * Collect the same live compatibility fingerprints used by production
     * planning without minting planning or mutation authority.
     *
     * This diagnostic surface deliberately does not require the quest to be in
     * the reviewed registry: its output is the evidence needed to review and
     * add such an entry. Callers must never reinterpret the returned facts as a
     * PartyQuestRuntimeCanonicalAuthorization or process planning request.
     */
    [[nodiscard]] static std::optional<PartyQuestRuntimeCompatibilityFacts>
    ObserveDiagnostic(
        TESQuest* apQuest,
        const ModSystem& acModSystem,
        const GameId& acExpectedQuestId,
        const PartyQuestCompatibilityEnvironmentFingerprints& acEnvironment) noexcept;

    [[nodiscard]] static std::optional<PartyQuestRuntimeProcessPlanningEvidence>
    ObserveFresh(
        TESQuest* apQuest,
        const ModSystem& acModSystem,
        const PartyQuestRuntimeCanonicalCandidate& acCandidate,
        const PartyQuestCompatibilityEnvironmentFingerprints& acEnvironment) noexcept;
};
