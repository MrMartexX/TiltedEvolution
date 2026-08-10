#pragma once

#include <Structs/Skyrim/QuestSnapshot.h>

#include <cstddef>

/** Immutable in-memory and wire bounds shared by canonical producers and decoders. */
struct PartyQuestResourcePolicy
{
    static constexpr size_t MaxWireQuestEntries = 8192;
    static constexpr size_t MaxSnapshotCollectionEntries = 4096;

    [[nodiscard]] static bool IsSnapshotWithinBounds(
        const QuestSnapshot& acSnapshot) noexcept
    {
        return acSnapshot.CompletedStages.size() <= MaxSnapshotCollectionEntries &&
            acSnapshot.Objectives.size() <= MaxSnapshotCollectionEntries &&
            acSnapshot.ReferenceAliases.size() <= MaxSnapshotCollectionEntries &&
            acSnapshot.LocationAliases.size() <= MaxSnapshotCollectionEntries &&
            acSnapshot.CreatedReferences.size() <= MaxSnapshotCollectionEntries;
    }
};
