#pragma once

#include <Structs/Skyrim/PartyQuestCoopSaveLayout.h>

#include <cstdint>
#include <filesystem>

enum class PartyQuestReplicaWorkspaceLeaseStatus : uint8_t
{
    NotAttempted,
    Acquired,
    Busy,
    InvalidIdentity,
    InvalidLayout,
    InvalidNamespace,
    IoError
};

/**
 * Kernel-backed exclusive lease for one exact campaign/player replica tree.
 *
 * The persistent lock file is never deleted during release. Ownership is the
 * live OS handle/descriptor, so process exit releases it without PID or age
 * heuristics and without replacing a locked inode behind another process.
 */
class PartyQuestReplicaWorkspaceLease final
{
public:
    PartyQuestReplicaWorkspaceLease() noexcept = default;
    ~PartyQuestReplicaWorkspaceLease() noexcept;

    PartyQuestReplicaWorkspaceLease(const PartyQuestReplicaWorkspaceLease&) = delete;
    PartyQuestReplicaWorkspaceLease& operator=(const PartyQuestReplicaWorkspaceLease&) = delete;
    PartyQuestReplicaWorkspaceLease(PartyQuestReplicaWorkspaceLease&& aRhs) noexcept;
    PartyQuestReplicaWorkspaceLease& operator=(PartyQuestReplicaWorkspaceLease&& aRhs) noexcept;

    [[nodiscard]] PartyQuestReplicaWorkspaceLeaseStatus Acquire(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId) noexcept;

    void Release() noexcept;

    [[nodiscard]] bool IsHeld() const noexcept { return m_nativeHandle != kInvalidHandle; }
    [[nodiscard]] const std::filesystem::path& GetLockPath() const noexcept { return m_lockPath; }

private:
    static constexpr intptr_t kInvalidHandle = -1;

    intptr_t m_nativeHandle{kInvalidHandle};
    std::filesystem::path m_lockPath;
};
