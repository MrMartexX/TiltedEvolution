#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_set>

/**
 * Process-local evidence that a concrete Skyrim reference is currently loaded.
 *
 * Observations are scoped to PartyQuestRuntimeGenerationFence. Evidence from an
 * older generation is never reused, and queries acquire the same shared
 * execution lease used by runtime mutation dispatch so a lifecycle/mod-mapping
 * invalidation cannot race a positive readiness result.
 *
 * This class deliberately knows only local Skyrim FormIDs. Mapping canonical
 * GameIds and deciding whether location/scene dependencies are ready remains a
 * higher-level responsibility.
 */
class PartyQuestRuntimeReferenceReadiness final
{
public:
    static constexpr size_t MaxTrackedReferences = 4096;

    PartyQuestRuntimeReferenceReadiness() noexcept;
    explicit PartyQuestRuntimeReferenceReadiness(
        PartyQuestRuntimeGenerationFence& aGenerationFence) noexcept;

    /**
     * Record one authoritative local loaded/unloaded observation.
     *
     * Returns false when the observation cannot be attached to a stable current
     * generation (for example while a lifecycle transition is pending) or when
     * the tracker has entered fail-closed overflow for this generation.
     */
    [[nodiscard]] bool Observe(uint32_t aFormId, bool aLoaded) noexcept;

    /**
     * Query readiness for an exact expected runtime generation.
     *
     * A positive result retains the generation execution lease for the complete
     * query, so invalidation cannot interleave between generation validation and
     * reading the evidence set.
     */
    [[nodiscard]] bool IsLoaded(
        uint32_t aFormId,
        uint64_t aExpectedGeneration) const noexcept;

    [[nodiscard]] uint64_t GetObservationGeneration() const noexcept;
    [[nodiscard]] bool IsOverflowed() const noexcept;

private:
    void ResetLocked(uint64_t aGeneration) noexcept;

    PartyQuestRuntimeGenerationFence& m_generationFence;
    mutable std::mutex m_mutex;
    uint64_t m_observationGeneration{};
    std::unordered_set<uint32_t> m_loadedReferences;
    bool m_overflowed{};
};
