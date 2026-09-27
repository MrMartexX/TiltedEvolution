#pragma once

#include <Structs/Skyrim/PartyQuestCoopSaveLayout.h>

#include <cstdint>
#include <filesystem>
#include <memory>

struct PartyQuestReplicaWorkspaceLeaseState;

/**
 * Process-local proof that the exact campaign/player workspace is protected by
 * a live kernel-backed lease. The capability shares ownership of the native
 * lease state, so releasing the originating lease object cannot unlock the
 * workspace while a publication still holds this proof.
 *
 * A default-constructed or moved-from capability is intentionally unverified.
 * Only PartyQuestReplicaWorkspaceLease can create a verified capability. Copies
 * are safe proof copies: they cannot change authority and each pins the same
 * already-acquired native lease state until that proof is released.
 */
class PartyQuestReplicaWorkspacePublicationCapability final
{
public:
    PartyQuestReplicaWorkspacePublicationCapability() noexcept = default;
    ~PartyQuestReplicaWorkspacePublicationCapability() noexcept = default;

    PartyQuestReplicaWorkspacePublicationCapability(
        const PartyQuestReplicaWorkspacePublicationCapability&) noexcept = default;
    PartyQuestReplicaWorkspacePublicationCapability& operator=(
        const PartyQuestReplicaWorkspacePublicationCapability&) noexcept = default;
    PartyQuestReplicaWorkspacePublicationCapability(
        PartyQuestReplicaWorkspacePublicationCapability&&) noexcept = default;
    PartyQuestReplicaWorkspacePublicationCapability& operator=(
        PartyQuestReplicaWorkspacePublicationCapability&&) noexcept = default;

    [[nodiscard]] bool IsVerified() const noexcept
    {
        return static_cast<bool>(m_state);
    }

    [[nodiscard]] bool Protects(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId) const noexcept;

    /**
     * Establishes the exact metadata + sidecar directory namespace needed by
     * the runtime control plane as power-loss durable while this capability
     * pins the kernel-backed workspace lease.
     *
     * POSIX uses PartyQuestStableStorage's component-wise directory publication
     * proof. Windows deliberately returns false because no reviewed generic
     * directory creation/flush contract exists there yet. Failure never weakens
     * the lease or grants strong durability; callers may retain a separately
     * labelled process-crash writer when their policy permits it.
     */
    [[nodiscard]] bool PreparePowerLossDurableRuntimeNamespace(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId) const noexcept;

private:
    explicit PartyQuestReplicaWorkspacePublicationCapability(
        std::shared_ptr<const PartyQuestReplicaWorkspaceLeaseState> aState) noexcept
        : m_state(std::move(aState))
    {
    }

    std::shared_ptr<const PartyQuestReplicaWorkspaceLeaseState> m_state;

    friend class PartyQuestReplicaWorkspaceLease;
};

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
 *
 * CreatePublicationCapability() may pin that exact native lease state for a
 * bounded filesystem publication. A second Acquire(), even in this process,
 * remains exclusive and must still reach the OS lock rather than borrowing the
 * capability path.
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

    [[nodiscard]] bool IsHeld() const noexcept;
    /** Process-local capability check; this grants no remote path authority. */
    [[nodiscard]] bool Protects(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId) const noexcept;

    [[nodiscard]] PartyQuestReplicaWorkspacePublicationCapability
    CreatePublicationCapability(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId) const noexcept;

    [[nodiscard]] const std::filesystem::path& GetLockPath() const noexcept;

private:
    std::shared_ptr<PartyQuestReplicaWorkspaceLeaseState> m_state;
};