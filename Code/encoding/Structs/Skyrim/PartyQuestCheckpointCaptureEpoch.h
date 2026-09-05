#pragma once

#include <chrono>
#include <cstdint>

class PartyQuestRuntimeGuardedSession;
class PartyQuestRuntimePreRepairCheckpointTestAccess;

/**
 * Encapsulation-backed identity for one logical PreRepair capture attempt.
 *
 * The epoch is intentionally process-local and non-durable. A restart before
 * the runtime mutation barrier already invalidates pre-mutation work and must
 * produce a fresh plan/capture, so resurrecting an old temporal context would
 * be unsafe. Every production token also carries an immutable monotonic local
 * deadline; expiry removes publication authority but cannot interrupt a
 * synchronous Skyrim engine call already in progress. The token is not a
 * cryptographic security primitive.
 */
class PartyQuestCheckpointCaptureEpoch final
{
public:
    PartyQuestCheckpointCaptureEpoch() noexcept = default;

    [[nodiscard]] bool IsVerified() const noexcept { return m_verified; }
    [[nodiscard]] uint64_t GetEpochId() const noexcept { return m_epochId; }
    [[nodiscard]] uint64_t GetTransactionId() const noexcept { return m_transactionId; }
    [[nodiscard]] uint64_t GetTargetWorldRevision() const noexcept
    {
        return m_targetWorldRevision;
    }
    [[nodiscard]] uint64_t GetSidecarManifestFingerprint() const noexcept
    {
        return m_sidecarManifestFingerprint;
    }
    [[nodiscard]] uint64_t GetDeadlineTicks() const noexcept { return m_deadlineTicks; }
    [[nodiscard]] bool IsExpired() const noexcept
    {
        return m_verified && MonotonicNowTicks() >= m_deadlineTicks;
    }

    [[nodiscard]] bool MatchesContext(
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision,
        uint64_t aSidecarManifestFingerprint) const noexcept
    {
        return m_verified &&
            !IsExpired() &&
            m_epochId != 0 &&
            aTransactionId == m_transactionId &&
            aTargetWorldRevision == m_targetWorldRevision &&
            aSidecarManifestFingerprint == m_sidecarManifestFingerprint;
    }

private:
    PartyQuestCheckpointCaptureEpoch(
        uint64_t aEpochId,
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision,
        uint64_t aSidecarManifestFingerprint,
        uint64_t aDeadlineTicks) noexcept
        : m_epochId(aEpochId)
        , m_transactionId(aTransactionId)
        , m_targetWorldRevision(aTargetWorldRevision)
        , m_sidecarManifestFingerprint(aSidecarManifestFingerprint)
        , m_deadlineTicks(aDeadlineTicks)
        , m_verified(
              aEpochId != 0 &&
              aTransactionId != 0 &&
              aTargetWorldRevision != 0 &&
              aSidecarManifestFingerprint != 0 &&
              aDeadlineTicks != 0)
    {
    }

    uint64_t m_epochId{};
    uint64_t m_transactionId{};
    uint64_t m_targetWorldRevision{};
    uint64_t m_sidecarManifestFingerprint{};
    uint64_t m_deadlineTicks{};
    bool m_verified{};

    [[nodiscard]] static uint64_t MonotonicNowTicks() noexcept
    {
        const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        return now > 0 ? static_cast<uint64_t>(now) : 0;
    }

    friend class PartyQuestRuntimeGuardedSession;
    // Defined only in Code/tests; no production constructor/API exists.
    friend class PartyQuestRuntimePreRepairCheckpointTestAccess;
};

enum class PartyQuestCheckpointCaptureEpochStatus : uint8_t
{
    Ready,
    InvalidRuntimeState,
    GuardMismatch,
    AlreadyActive
};

struct PartyQuestCheckpointCaptureEpochResult
{
    PartyQuestCheckpointCaptureEpochStatus Status{
        PartyQuestCheckpointCaptureEpochStatus::InvalidRuntimeState};
    PartyQuestCheckpointCaptureEpoch Epoch;

    [[nodiscard]] bool IsReady() const noexcept
    {
        return Status == PartyQuestCheckpointCaptureEpochStatus::Ready &&
            Epoch.IsVerified();
    }
};
