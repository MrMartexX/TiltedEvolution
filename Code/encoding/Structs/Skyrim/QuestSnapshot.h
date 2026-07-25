#pragma once

#include <Structs/GameId.h>

#include <cstdint>
#include <optional>
#include <vector>

enum class QuestSnapshotStatus : uint8_t
{
    Inactive,
    Running,
    Stopped,
    Completed
};

enum class QuestObjectiveState : uint8_t
{
    Hidden,
    Displayed,
    Completed,
    Failed
};

struct QuestObjectiveSnapshot
{
    uint16_t Index{};
    QuestObjectiveState State{QuestObjectiveState::Hidden};

    bool operator==(const QuestObjectiveSnapshot&) const noexcept = default;
};

struct QuestReferenceAliasSnapshot
{
    uint32_t AliasId{};
    std::optional<GameId> ReferenceId;
    bool IsQuestObject{};

    bool operator==(const QuestReferenceAliasSnapshot&) const noexcept = default;
};

struct QuestLocationAliasSnapshot
{
    uint32_t AliasId{};
    std::optional<GameId> LocationId;

    bool operator==(const QuestLocationAliasSnapshot&) const noexcept = default;
};

/**
 * Canonical, game-independent representation of the quest fields used by the
 * equal-party quest synchronization proof of concept.
 *
 * The first PoC intentionally excludes arbitrary Papyrus VM variables. The
 * structure is designed so collectors can be added without changing the
 * deterministic digest contract.
 */
struct QuestSnapshot
{
    static constexpr uint16_t SchemaVersion = 1;

    GameId QuestId{};
    QuestSnapshotStatus Status{QuestSnapshotStatus::Inactive};
    uint16_t CurrentStage{};
    uint64_t Revision{};
    uint32_t InitiatorPlayerId{};
    std::optional<uint32_t> SceneParticipantPlayerId;

    std::vector<uint16_t> CompletedStages;
    std::vector<QuestObjectiveSnapshot> Objectives;
    std::vector<QuestReferenceAliasSnapshot> ReferenceAliases;
    std::vector<QuestLocationAliasSnapshot> LocationAliases;
    std::vector<GameId> CreatedReferences;

    /** Sorts unordered fields and removes exact duplicates. */
    void Canonicalize();

    /** Returns a deterministic FNV-1a digest of the canonical snapshot. */
    [[nodiscard]] uint64_t ComputeDigest() const;

    bool operator==(const QuestSnapshot&) const noexcept = default;
};
