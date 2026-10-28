#pragma once

#include <Structs/Skyrim/PartyQuestCompatibilityAdmission.h>
#include <Structs/Skyrim/PartyQuestRuntimeCompatibility.h>

#include <span>

/**
 * Immutable reviewed production quest compatibility and transition registry.
 *
 * Requirements and transition edges encoded here have undergone offline static
 * analysis, Papyrus fragment audit, and runtime compatibility verification.
 * Generated data alone does not authorize mutation or skip runtime gates.
 */
class PartyQuestReviewedQuestProfiles final
{
public:
    static constexpr uint32_t ProfileVersion = 1;
    static constexpr uint64_t NativeAdapterFingerprint = 0x5354525153544147ull; // "STRQSTAG"

    // Reviewed candidate 1: dunNorthwatchKeepQST (0x000C07C1)
    static constexpr uint32_t NorthwatchModId = 0;
    static constexpr uint32_t NorthwatchBaseId = 0x000C07C1;
    static constexpr uint16_t NorthwatchSourceStage = 0;
    static constexpr uint16_t NorthwatchTargetStage = 100;
    static constexpr uint64_t NorthwatchResolvedRecordFingerprint = 0x29C2FECA6CAAB0B0ull;
    static constexpr uint64_t NorthwatchWinningOverrideFingerprint = 0xE0DE05964910A9ACull;
    static constexpr uint64_t NorthwatchAnalyzerTopologyFingerprint = 0x74F3BB2F41AF7AECull;
    static constexpr uint64_t NorthwatchAnalyzerOverrideFingerprint = 0xE512EED047636F89ull;

    // Reviewed candidate 2: dunKarthspireRedoubtQST (0x000357E5)
    static constexpr uint32_t KarthspireModId = 0;
    static constexpr uint32_t KarthspireBaseId = 0x000357E5;
    static constexpr uint16_t KarthspireSourceStage = 10;
    static constexpr uint16_t KarthspireTargetStage = 100;
    static constexpr uint64_t KarthspireResolvedRecordFingerprint = 0x3351A4356804FA93ull;
    static constexpr uint64_t KarthspireWinningOverrideFingerprint = 0x569249222D271063ull;
    static constexpr uint64_t KarthspireAnalyzerTopologyFingerprint = 0x8B054D9B48727D59ull;
    static constexpr uint64_t KarthspireAnalyzerOverrideFingerprint = 0x970D7DA42342C1A2ull;

    // Common reference environment for Skyrim SE 1.6.1170
    static constexpr uint64_t ReferencePluginEnvironment = 0x62557D41E3E9A150ull;
    static constexpr uint64_t ReferenceScriptEnvironment = 0x28AE1C01A4A2EAD7ull;
    static constexpr uint64_t ReferenceAnalyzerProvenance = 0x6D22E810470CE788ull;

    [[nodiscard]] static PartyQuestRuntimeCompatibilityManifest BuildReviewedManifest() noexcept;

    [[nodiscard]] static bool HasReviewedProfile(const GameId& acQuestId) noexcept;

    [[nodiscard]] static const PartyQuestRuntimeCompatibilityRequirement* FindRequirement(const GameId& acQuestId) noexcept;

    [[nodiscard]] static PartyQuestReviewedTransitionManifest BuildReviewedTransitionManifest() noexcept;

    [[nodiscard]] static const PartyQuestReviewedTransition* FindReviewedTransition(const PartyQuestTransitionIdentity& acIdentity) noexcept;
    [[nodiscard]] static const PartyQuestReviewedTransition* FindReviewedTransition(const GameId& acQuestId, uint16_t aSourceStage, uint16_t aTargetStage) noexcept;

    [[nodiscard]] static std::span<const PartyQuestRuntimeCompatibilityRequirement> GetReviewedRequirements() noexcept;

    [[nodiscard]] static std::span<const PartyQuestReviewedTransition> GetReviewedTransitions() noexcept;
};
