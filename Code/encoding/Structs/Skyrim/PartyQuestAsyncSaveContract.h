#pragma once

#include <Structs/Skyrim/PartyQuestCampaign.h>
#include <Structs/Skyrim/PartyQuestPlayerProfile.h>

#include <cstdint>
#include <optional>
#include <string>

enum class PartyQuestAsyncSaveArtifact : uint8_t
{
    SkyrimEss,
    SkseCosave
};

enum class PartyQuestAsyncSaveArtifactOutcome : uint8_t
{
    ClosedSuccess,
    Failed
};

enum class PartyQuestAsyncSaveContractStatus : uint8_t
{
    Inactive,
    Pending,
    Complete,
    Busy,
    Stale,
    Duplicate,
    Failed,
    TimedOut,
    Cancelled,
    InvalidIdentity,
    InvalidClock,
    ExistingFileConflict
};

struct PartyQuestAsyncSaveRequestIdentity
{
    PartyQuestCampaignId CampaignId;
    PartyQuestPlayerProfileId PlayerProfileId;
    uint64_t RuntimeGeneration{};
    uint64_t TransactionId{};
    uint64_t TargetWorldRevision{};
    uint64_t CaptureEpochId{};
    uint64_t AttemptNonce{};
    std::string SaveName;

    [[nodiscard]] bool IsValid() const noexcept;
    bool operator==(const PartyQuestAsyncSaveRequestIdentity&) const noexcept = default;
};

/**
 * Move-only proof that both exact artifacts reached an authoritative closed
 * success notification for one request. It is not issued from file existence,
 * size, timestamps or Save_Impl's return value.
 */
class PartyQuestAsyncSaveCompletion final
{
public:
    PartyQuestAsyncSaveCompletion() noexcept = default;
    PartyQuestAsyncSaveCompletion(PartyQuestAsyncSaveCompletion&& aOther) noexcept;
    PartyQuestAsyncSaveCompletion& operator=(PartyQuestAsyncSaveCompletion&& aOther) noexcept;
    PartyQuestAsyncSaveCompletion(const PartyQuestAsyncSaveCompletion&) = delete;
    PartyQuestAsyncSaveCompletion& operator=(const PartyQuestAsyncSaveCompletion&) = delete;

    [[nodiscard]] bool IsValid() const noexcept { return m_nonce != 0; }
    [[nodiscard]] bool Matches(const PartyQuestAsyncSaveRequestIdentity& acIdentity) const noexcept;

private:
    friend class PartyQuestAsyncSaveContract;
    PartyQuestAsyncSaveRequestIdentity m_identity;
    uint64_t m_nonce{};
};

struct PartyQuestAsyncSaveContractResult
{
    PartyQuestAsyncSaveContractStatus Status{PartyQuestAsyncSaveContractStatus::Inactive};
    bool SkyrimEssClosed{};
    bool SkseCosaveClosed{};
    bool CleanupRequired{};
    std::optional<PartyQuestAsyncSaveCompletion> Completion;
};

/**
 * Pure request-correlation state machine for a future read-only Skyrim/SKSE
 * completion observer. Only one request may own the engine save pipeline.
 * All lifecycle/failure terminals require cleanup when either artifact may
 * have been created. This class performs no I/O and grants no engine authority.
 */
class PartyQuestAsyncSaveContract final
{
public:
    static constexpr uint64_t kTimeoutMs = 30000;

    [[nodiscard]] PartyQuestAsyncSaveContractResult Begin(
        PartyQuestAsyncSaveRequestIdentity aIdentity,
        uint64_t aNowMs,
        bool aMainPathExists,
        bool aCosavePathExists) noexcept;

    [[nodiscard]] PartyQuestAsyncSaveContractResult Observe(
        const PartyQuestAsyncSaveRequestIdentity& acIdentity,
        PartyQuestAsyncSaveArtifact aArtifact,
        PartyQuestAsyncSaveArtifactOutcome aOutcome,
        uint64_t aNowMs) noexcept;

    [[nodiscard]] PartyQuestAsyncSaveContractResult Poll(uint64_t aNowMs) noexcept;
    [[nodiscard]] PartyQuestAsyncSaveContractResult Cancel(
        const PartyQuestAsyncSaveRequestIdentity& acIdentity) noexcept;
    void Retire() noexcept;

private:
    [[nodiscard]] PartyQuestAsyncSaveContractResult Result(
        PartyQuestAsyncSaveContractStatus aStatus) const noexcept;
    [[nodiscard]] PartyQuestAsyncSaveContractResult Fail(
        PartyQuestAsyncSaveContractStatus aStatus) noexcept;
    [[nodiscard]] bool CheckClock(uint64_t aNowMs) noexcept;

    std::optional<PartyQuestAsyncSaveRequestIdentity> m_identity;
    uint64_t m_startedAtMs{};
    uint64_t m_lastNowMs{};
    uint64_t m_completionNonce{1};
    bool m_mainClosed{};
    bool m_cosaveClosed{};
    PartyQuestAsyncSaveContractStatus m_status{PartyQuestAsyncSaveContractStatus::Inactive};
};
