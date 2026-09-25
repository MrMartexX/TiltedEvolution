#pragma once

#include <Structs/Skyrim/PartyQuestAsyncSaveContract.h>

#include <cstdint>
#include <optional>

enum class PartyQuestAsyncSaveFinalOutcome : uint8_t
{
    Succeeded,
    Failed
};

enum class PartyQuestAsyncSaveFinalizationStatus : uint8_t
{
    Inactive,
    Pending,
    AwaitingRetirement,
    Finalized,
    RetiredWithoutSuccess,
    Busy,
    Stale,
    Duplicate,
    InvalidInput,
    ProtocolViolation,
    ProtocolViolationRetired,
    ContractMismatch
};

struct PartyQuestAsyncSaveFinalizationResult
{
    PartyQuestAsyncSaveFinalizationStatus Status{
        PartyQuestAsyncSaveFinalizationStatus::Inactive};
    bool RetirementApplied{};
    std::optional<PartyQuestAsyncSaveCompletion> Completion;
};

/**
 * Pure correlation gate between logical artifact completion and an
 * authenticated request-wide native retirement event. This object provides
 * no source authentication, lifecycle lease, durability or mutation authority.
 * Its owner must serialize all calls.
 */
class PartyQuestAsyncSaveFinalizationGate final
{
public:
    /**
     * Starts the logical contract and correlation gate as one fail-closed
     * admission boundary. If gate admission fails after contract admission,
     * the not-yet-enqueued contract is cancelled and retired before return.
     */
    [[nodiscard]] PartyQuestAsyncSaveFinalizationResult BeginCoordinated(
        PartyQuestAsyncSaveContract& aContract,
        const PartyQuestAsyncSaveRequestIdentity& acIdentity,
        uint64_t aNowMs,
        bool aMainPathExists,
        bool aCosavePathExists) noexcept;

    [[nodiscard]] PartyQuestAsyncSaveFinalizationResult Begin(
        const PartyQuestAsyncSaveRequestIdentity& acIdentity) noexcept;

    [[nodiscard]] PartyQuestAsyncSaveFinalizationResult ObserveContractResult(
        const PartyQuestAsyncSaveRequestIdentity& acIdentity,
        PartyQuestAsyncSaveContractResult&& aResult) noexcept;

    [[nodiscard]] PartyQuestAsyncSaveFinalizationResult ObserveRetirement(
        PartyQuestAsyncSaveContract& aContract,
        const PartyQuestAsyncSaveRequestIdentity& acIdentity,
        PartyQuestAsyncSaveFinalOutcome aOutcome) noexcept;

private:
    enum class State : uint8_t
    {
        Inactive,
        Pending,
        LogicalSuccess,
        LogicalFailure,
        ProtocolFailure,
        Finalized,
        RetiredWithoutSuccess
    };

    [[nodiscard]] PartyQuestAsyncSaveFinalizationResult Result(
        PartyQuestAsyncSaveFinalizationStatus aStatus,
        bool aRetirementApplied = false) const noexcept;

    std::optional<PartyQuestAsyncSaveRequestIdentity> m_identity;
    std::optional<PartyQuestAsyncSaveCompletion> m_completion;
    State m_state{State::Inactive};
};
