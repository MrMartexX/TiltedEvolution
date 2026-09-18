#include <Structs/Skyrim/PartyQuestAsyncSaveFinalizationGate.h>

#include <utility>

namespace
{
bool IsKnownFinalOutcome(PartyQuestAsyncSaveFinalOutcome aOutcome) noexcept
{
    return aOutcome == PartyQuestAsyncSaveFinalOutcome::Succeeded ||
           aOutcome == PartyQuestAsyncSaveFinalOutcome::Failed;
}

bool IsLogicalFailure(PartyQuestAsyncSaveContractStatus aStatus) noexcept
{
    return aStatus == PartyQuestAsyncSaveContractStatus::Failed ||
           aStatus == PartyQuestAsyncSaveContractStatus::TimedOut ||
           aStatus == PartyQuestAsyncSaveContractStatus::Cancelled ||
           aStatus == PartyQuestAsyncSaveContractStatus::InvalidClock;
}

bool IsCompleteResultWellFormed(
    const PartyQuestAsyncSaveRequestIdentity& acIdentity,
    const PartyQuestAsyncSaveContractResult& acResult) noexcept
{
    return acResult.Status == PartyQuestAsyncSaveContractStatus::Complete &&
           acResult.SkyrimEssClosed && acResult.SkseCosaveClosed &&
           acResult.SkyrimEssPublished && acResult.SkseCosavePublished &&
           !acResult.CleanupRequired && acResult.Completion.has_value() &&
           acResult.Completion->Matches(acIdentity);
}
}

PartyQuestAsyncSaveFinalizationResult PartyQuestAsyncSaveFinalizationGate::Result(
    PartyQuestAsyncSaveFinalizationStatus aStatus,
    bool aRetirementApplied) const noexcept
{
    PartyQuestAsyncSaveFinalizationResult result;
    result.Status = aStatus;
    result.RetirementApplied = aRetirementApplied;
    return result;
}

PartyQuestAsyncSaveFinalizationResult PartyQuestAsyncSaveFinalizationGate::Begin(
    const PartyQuestAsyncSaveRequestIdentity& acIdentity) noexcept
{
    if (!acIdentity.IsValid())
        return Result(PartyQuestAsyncSaveFinalizationStatus::InvalidInput);
    if (m_state == State::Pending || m_state == State::LogicalSuccess ||
        m_state == State::LogicalFailure || m_state == State::ProtocolFailure)
        return Result(PartyQuestAsyncSaveFinalizationStatus::Busy);

    try
    {
        PartyQuestAsyncSaveRequestIdentity ownedIdentity = acIdentity;
        m_identity.emplace(std::move(ownedIdentity));
        m_completion.reset();
        m_state = State::Pending;
        return Result(PartyQuestAsyncSaveFinalizationStatus::Pending);
    }
    catch (...)
    {
        return Result(PartyQuestAsyncSaveFinalizationStatus::InvalidInput);
    }
}

PartyQuestAsyncSaveFinalizationResult
PartyQuestAsyncSaveFinalizationGate::ObserveContractResult(
    const PartyQuestAsyncSaveRequestIdentity& acIdentity,
    PartyQuestAsyncSaveContractResult&& aResult) noexcept
{
    if (!acIdentity.IsValid())
        return Result(PartyQuestAsyncSaveFinalizationStatus::InvalidInput);
    if (!m_identity || *m_identity != acIdentity)
        return Result(PartyQuestAsyncSaveFinalizationStatus::Stale);
    if (m_state == State::Finalized || m_state == State::RetiredWithoutSuccess)
        return Result(PartyQuestAsyncSaveFinalizationStatus::Duplicate);
    if (aResult.Status == PartyQuestAsyncSaveContractStatus::Duplicate)
        return Result(PartyQuestAsyncSaveFinalizationStatus::Duplicate);

    if (aResult.Status == PartyQuestAsyncSaveContractStatus::Pending)
    {
        if (aResult.Completion.has_value())
            return Result(PartyQuestAsyncSaveFinalizationStatus::InvalidInput);
        return Result(m_state == State::Pending ?
            PartyQuestAsyncSaveFinalizationStatus::Pending :
            PartyQuestAsyncSaveFinalizationStatus::ProtocolViolation);
    }

    if (aResult.Status == PartyQuestAsyncSaveContractStatus::Complete)
    {
        if (m_state == State::LogicalSuccess)
            return Result(PartyQuestAsyncSaveFinalizationStatus::Duplicate);
        if (m_state != State::Pending ||
            !IsCompleteResultWellFormed(acIdentity, aResult))
            return Result(m_state == State::LogicalFailure ||
                m_state == State::ProtocolFailure ?
                PartyQuestAsyncSaveFinalizationStatus::ProtocolViolation :
                PartyQuestAsyncSaveFinalizationStatus::InvalidInput);

        m_completion.emplace(std::move(*aResult.Completion));
        m_state = State::LogicalSuccess;
        return Result(PartyQuestAsyncSaveFinalizationStatus::AwaitingRetirement);
    }

    if (IsLogicalFailure(aResult.Status))
    {
        if (aResult.Completion.has_value())
            return Result(PartyQuestAsyncSaveFinalizationStatus::InvalidInput);
        if (m_state == State::LogicalFailure || m_state == State::ProtocolFailure)
            return Result(PartyQuestAsyncSaveFinalizationStatus::Duplicate);
        if (m_state == State::LogicalSuccess)
        {
            m_completion.reset();
            m_state = State::ProtocolFailure;
            return Result(PartyQuestAsyncSaveFinalizationStatus::ProtocolViolation);
        }
        if (m_state != State::Pending)
            return Result(PartyQuestAsyncSaveFinalizationStatus::ProtocolViolation);

        m_state = State::LogicalFailure;
        return Result(PartyQuestAsyncSaveFinalizationStatus::AwaitingRetirement);
    }

    return Result(PartyQuestAsyncSaveFinalizationStatus::InvalidInput);
}

PartyQuestAsyncSaveFinalizationResult
PartyQuestAsyncSaveFinalizationGate::ObserveRetirement(
    PartyQuestAsyncSaveContract& aContract,
    const PartyQuestAsyncSaveRequestIdentity& acIdentity,
    PartyQuestAsyncSaveFinalOutcome aOutcome) noexcept
{
    if (!acIdentity.IsValid() || !IsKnownFinalOutcome(aOutcome))
        return Result(PartyQuestAsyncSaveFinalizationStatus::InvalidInput);
    if (!m_identity || *m_identity != acIdentity)
        return Result(PartyQuestAsyncSaveFinalizationStatus::Stale);
    if (m_state == State::Finalized || m_state == State::RetiredWithoutSuccess)
        return Result(PartyQuestAsyncSaveFinalizationStatus::Duplicate);
    if (m_state == State::Pending)
    {
        // PQS4 is authoritative request-wide drain evidence even when its
        // matching terminal PQS3 was lost or arrived out of order. Never turn
        // that protocol violation into success, but do consume the one-shot
        // drain proof so this exact request cannot wedge ownership forever.
        const auto cancelled = aContract.Cancel(acIdentity);
        if (cancelled.Status != PartyQuestAsyncSaveContractStatus::Cancelled)
            return Result(PartyQuestAsyncSaveFinalizationStatus::ContractMismatch);
        const auto retired = aContract.Retire(acIdentity);
        if (retired.Status != PartyQuestAsyncSaveContractStatus::Inactive)
            return Result(PartyQuestAsyncSaveFinalizationStatus::ContractMismatch);
        m_completion.reset();
        m_state = State::RetiredWithoutSuccess;
        return Result(
            PartyQuestAsyncSaveFinalizationStatus::ProtocolViolationRetired, true);
    }

    const bool logicalSuccess = m_state == State::LogicalSuccess;
    const bool logicalFailure = m_state == State::LogicalFailure;
    const bool protocolFailure = m_state == State::ProtocolFailure;

    if (aOutcome == PartyQuestAsyncSaveFinalOutcome::Succeeded && !logicalSuccess)
    {
        const auto retired = aContract.Retire(acIdentity);
        if (retired.Status != PartyQuestAsyncSaveContractStatus::Inactive)
            return Result(PartyQuestAsyncSaveFinalizationStatus::ContractMismatch);
        m_completion.reset();
        m_state = State::RetiredWithoutSuccess;
        return Result(
            PartyQuestAsyncSaveFinalizationStatus::ProtocolViolationRetired, true);
    }

    if (aOutcome == PartyQuestAsyncSaveFinalOutcome::Failed)
    {
        if (!logicalSuccess && !logicalFailure && !protocolFailure)
            return Result(PartyQuestAsyncSaveFinalizationStatus::ProtocolViolation);
        const auto retired = aContract.Retire(acIdentity);
        if (retired.Status != PartyQuestAsyncSaveContractStatus::Inactive)
            return Result(PartyQuestAsyncSaveFinalizationStatus::ContractMismatch);
        m_completion.reset();
        m_state = State::RetiredWithoutSuccess;
        return Result(
            PartyQuestAsyncSaveFinalizationStatus::RetiredWithoutSuccess, true);
    }

    if (!logicalSuccess || !m_completion ||
        !m_completion->Matches(acIdentity))
        return Result(PartyQuestAsyncSaveFinalizationStatus::ProtocolViolation);

    const auto retired = aContract.Retire(acIdentity);
    if (retired.Status != PartyQuestAsyncSaveContractStatus::Inactive)
    {
        m_completion.reset();
        m_state = State::ProtocolFailure;
        return Result(PartyQuestAsyncSaveFinalizationStatus::ContractMismatch);
    }

    PartyQuestAsyncSaveFinalizationResult result;
    result.Status = PartyQuestAsyncSaveFinalizationStatus::Finalized;
    result.RetirementApplied = true;
    result.Completion.emplace(std::move(*m_completion));
    m_completion.reset();
    m_state = State::Finalized;
    return result;
}
