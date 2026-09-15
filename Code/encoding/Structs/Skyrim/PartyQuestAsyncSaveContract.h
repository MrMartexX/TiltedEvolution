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

enum class PartyQuestAsyncSavePublicationOutcome : uint8_t
{
    PublishedSuccess,
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
 * Move-only proof that both exact artifacts reached authoritative checked
 * close and final-name publication success for one request. It is not issued
 * from file existence, size, timestamps or Save_Impl's return value.
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
    bool SkyrimEssPublished{};
    bool SkseCosavePublished{};
    // Cleanup is required after physical retirement; this flag does not prove
    // that it is safe to touch files while the native request may still write.
    bool CleanupRequired{};
    std::optional<PartyQuestAsyncSaveCompletion> Completion;
};

/**
 * Pure request-correlation state machine for a future Skyrim/SKSE completion
 * provider. Only one request may own the engine save pipeline. Logical
 * terminals retain that ownership until matching physical I/O retirement.
 * This class performs no I/O, owns no threads and grants no engine authority.
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

    [[nodiscard]] PartyQuestAsyncSaveContractResult ObservePublication(
        const PartyQuestAsyncSaveRequestIdentity& acIdentity,
        PartyQuestAsyncSaveArtifact aArtifact,
        PartyQuestAsyncSavePublicationOutcome aOutcome,
        uint64_t aNowMs) noexcept;

    [[nodiscard]] PartyQuestAsyncSaveContractResult Poll(uint64_t aNowMs) noexcept;
    [[nodiscard]] PartyQuestAsyncSaveContractResult Cancel(
        const PartyQuestAsyncSaveRequestIdentity& acIdentity) noexcept;

    /**
     * Releases ownership only after the native provider has authoritatively
     * established that the matching request can no longer perform I/O. A
     * logical terminal status is not evidence that the external queue drained.
     * Pending requests must first receive a failure/cancellation outcome.
     */
    [[nodiscard]] PartyQuestAsyncSaveContractResult Retire(
        const PartyQuestAsyncSaveRequestIdentity& acIdentity) noexcept;

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
    bool m_mainPublished{};
    bool m_cosavePublished{};
    PartyQuestAsyncSaveContractStatus m_status{PartyQuestAsyncSaveContractStatus::Inactive};
};
