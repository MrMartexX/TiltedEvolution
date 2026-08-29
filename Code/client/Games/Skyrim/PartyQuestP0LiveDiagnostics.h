#pragma once

#include <Structs/Skyrim/QuestSnapshot.h>

#include <cstddef>
#include <cstdint>

struct ModSystem;
struct TESQuest;

/**
 * Read-only live evidence recorder for the equal-party quest P0 boundary.
 *
 * This surface is deliberately observational. It never grants runtime mutation
 * authority, never arms a mutation barrier and never changes canonical/wire or
 * persistence state. Unknown evidence is recorded as unavailable rather than
 * synthesized.
 *
 * Enable locally with either:
 *   STR_PARTY_QUEST_P0_LIVE_DIAGNOSTICS=1
 * or <TiltedPhoques::GetPath()>/party_quest_p0_live.ini:
 *   [Gameplay]
 *   bEnablePartyQuestP0LiveDiagnostics=true
 */
class PartyQuestP0LiveDiagnostics final
{
public:
    static void Initialize() noexcept;
    [[nodiscard]] static bool IsEnabled() noexcept;

    static void RecordRuntimeThreadObservation(
        bool aAccepted,
        uint32_t aBoundThreadId,
        uint32_t aCurrentThreadId) noexcept;

    static void RecordGenerationTransition(
        const char* acReason,
        const char* acPhase,
        uint64_t aGenerationBefore,
        uint64_t aGenerationAfter) noexcept;

    static void RecordTransportState(const char* acState) noexcept;
    static void RecordGamePresence(bool aInGame) noexcept;

    /** Emit the current process-local hook installation ledger. */
    static void RecordLifecycleCapabilities(const char* acPhase) noexcept;

    /**
     * Samples the exact-version Papyrus diagnostic adapter. Calls are throttled
     * internally and remain evidence-only; they cannot grant VM authority.
     */
    static void RecordPapyrusRuntimeObservation() noexcept;

    static void RecordEngineSave(
        const char* acPhase,
        const char* acFileName,
        uint64_t aTransactionId,
        int32_t aDeviceId,
        uint32_t aOutputStats,
        bool aPermitted,
        bool aResultKnown,
        bool aResult) noexcept;

    static void RecordModMappingBegin(
        size_t aServerModCount,
        uint64_t aGenerationBefore,
        uint64_t aGenerationAfter) noexcept;

    static void RecordModMappingEntry(
        uint32_t aServerModId,
        const char* acFilename,
        bool aServerLite,
        bool aResolvedLocally,
        uint32_t aLocalModId,
        bool aLocalLite) noexcept;

    static void RecordModMappingEnd(
        size_t aServerModCount,
        size_t aResolvedCount,
        size_t aMissingCount,
        uint64_t aGeneration) noexcept;

    static void RecordCompatibilityObservation(
        TESQuest* apQuest,
        const GameId& acExpectedQuestId,
        const ModSystem& acModSystem) noexcept;

    static void RecordQuestObservation(
        TESQuest* apQuest,
        const QuestSnapshot& acSnapshot,
        const ModSystem& acModSystem,
        const char* acReason) noexcept;
};
