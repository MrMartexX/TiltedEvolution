#pragma once

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
 * When a profile exists, ObserveFresh() independently fingerprints the resolved
 * live TESQuest topology, the ordered loaded plugin bytes and the installed
 * script/archive bytes under the mapped Skyrim Data directory. These
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

    [[nodiscard]] static std::optional<PartyQuestRuntimeProcessPlanningEvidence>
    ObserveFresh(
        TESQuest* apQuest,
        const ModSystem& acModSystem,
        const PartyQuestRuntimeCanonicalCandidate& acCandidate) noexcept;
};
