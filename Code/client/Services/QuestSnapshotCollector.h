#pragma once

#include <Structs/Skyrim/QuestSnapshot.h>

struct ModSystem;
struct TESQuest;

/**
 * Reads the runtime fields of a TESQuest into the network-independent
 * QuestSnapshot representation. This PoC is read-only: it never mutates the
 * quest, its aliases, or the player's save.
 */
struct QuestSnapshotCollector final
{
    [[nodiscard]] static std::optional<QuestSnapshot> Collect(TESQuest* apQuest, const ModSystem& acModSystem) noexcept;
    static void Log(TESQuest* apQuest, const QuestSnapshot& acSnapshot, const char* acReason) noexcept;
};
