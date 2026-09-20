#pragma once

#include <Structs/Skyrim/PartyQuestAsyncSaveContract.h>

#include <cstdint>
#include <optional>

enum class PartyQuestAsyncSaveEngineAdmissionOutcome : uint8_t
{
    Admitted,
    Rejected
};

enum class PartyQuestAsyncSavePhysicalOutcome : uint8_t
{
    Succeeded,
    Failed
};

enum class PartyQuestAsyncSaveLifecycleStatus : uint8_t
{
    Inactive,
    Active,
    CompletionAccepted,
    AdmissionClosed,
    RetiredAuthorized,
    RetiredWithoutAuthority,
    Busy,
    Stale,
    Duplicate,
    InvalidIdentity
};

struct PartyQuestAsyncSaveLifecycleResult
{
    PartyQuestAsyncSaveLifecycleStatus Status{
        PartyQuestAsyncSaveLifecycleStatus::Inactive};
    bool CancelRequired{};
    bool DrainRequired{};
    bool SafeToInvalidate{true};
    bool ConsumptionAuthorized{};
};

/**
 * Pure lifecycle gate for one engine-admitted asynchronous native save.
 *
 * Closing admission revokes logical authority immediately, but deliberately
 * retains the exact request identity until matching physical retirement. This
 * separates "must never publish" from "the external writer can no longer use
 * its capability". The owner must serialize calls. This type performs no I/O,
 * stores no pointers and grants no engine or canonical-mutation authority.
 */
class PartyQuestAsyncSaveLifecycle final
{
public:
    [[nodiscard]] PartyQuestAsyncSaveLifecycleResult ObserveEngineAdmission(
        const PartyQuestAsyncSaveRequestIdentity& acIdentity,
        PartyQuestAsyncSaveEngineAdmissionOutcome aOutcome) noexcept;

    [[nodiscard]] PartyQuestAsyncSaveLifecycleResult ObserveCompletion(
        const PartyQuestAsyncSaveRequestIdentity& acIdentity,
        PartyQuestAsyncSavePhysicalOutcome aOutcome) noexcept;

    [[nodiscard]] PartyQuestAsyncSaveLifecycleResult CloseAdmission() noexcept;

    [[nodiscard]] PartyQuestAsyncSaveLifecycleResult ObserveCancelRequested(
        const PartyQuestAsyncSaveRequestIdentity& acIdentity) noexcept;

    [[nodiscard]] PartyQuestAsyncSaveLifecycleResult ObserveRetirement(
        const PartyQuestAsyncSaveRequestIdentity& acIdentity) noexcept;

    [[nodiscard]] PartyQuestAsyncSaveLifecycleResult Current() const noexcept;

private:
    enum class State : uint8_t
    {
        Inactive,
        Active,
        CompletedSuccess,
        CompletedFailure,
        RetiredAuthorized,
        RetiredWithoutAuthority
    };

    [[nodiscard]] PartyQuestAsyncSaveLifecycleResult Result(
        PartyQuestAsyncSaveLifecycleStatus aStatus,
        bool aConsumptionAuthorized = false) const noexcept;
    [[nodiscard]] bool Matches(
        const PartyQuestAsyncSaveRequestIdentity& acIdentity) const noexcept;
    [[nodiscard]] bool HasActiveRequest() const noexcept;

    std::optional<PartyQuestAsyncSaveRequestIdentity> m_identity;
    State m_state{State::Inactive};
    bool m_admissionOpen{true};
    bool m_cancelRequested{};
};
