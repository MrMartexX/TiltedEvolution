#include <Structs/Skyrim/PartyQuestAsyncSaveLifecycle.h>

PartyQuestAsyncSaveLifecycleResult
PartyQuestAsyncSaveLifecycle::ObserveReservation(
    const PartyQuestAsyncSaveRequestIdentity& acIdentity,
    PartyQuestAsyncSaveReservationOutcome aOutcome) noexcept
{
    if (!acIdentity.IsValid())
        return Result(PartyQuestAsyncSaveLifecycleStatus::InvalidIdentity);

    if (aOutcome == PartyQuestAsyncSaveReservationOutcome::Rejected)
        return Result(HasActiveRequest() ?
                PartyQuestAsyncSaveLifecycleStatus::Busy :
                PartyQuestAsyncSaveLifecycleStatus::Inactive);

    if (!m_admissionOpen)
        return Result(PartyQuestAsyncSaveLifecycleStatus::AdmissionClosed);

    const bool retired = m_state == State::RetiredAuthorized ||
        m_state == State::RetiredWithoutAuthority;
    if (m_state != State::Inactive && !retired)
        return Result(Matches(acIdentity) ?
                PartyQuestAsyncSaveLifecycleStatus::Duplicate :
                PartyQuestAsyncSaveLifecycleStatus::Busy);
    if (retired && Matches(acIdentity))
        return Result(PartyQuestAsyncSaveLifecycleStatus::Duplicate);

    try
    {
        m_identity.emplace(acIdentity);
    }
    catch (...)
    {
        return Result(PartyQuestAsyncSaveLifecycleStatus::InvalidIdentity);
    }

    m_state = State::Active;
    m_cancelRequested = false;
    return Result(PartyQuestAsyncSaveLifecycleStatus::Active);
}

PartyQuestAsyncSaveLifecycleResult PartyQuestAsyncSaveLifecycle::ObserveCompletion(
    const PartyQuestAsyncSaveRequestIdentity& acIdentity,
    PartyQuestAsyncSavePhysicalOutcome aOutcome) noexcept
{
    if (!acIdentity.IsValid())
        return Result(PartyQuestAsyncSaveLifecycleStatus::InvalidIdentity);
    if (!Matches(acIdentity))
        return Result(PartyQuestAsyncSaveLifecycleStatus::Stale);

    if (m_state == State::RetiredAuthorized ||
        m_state == State::RetiredWithoutAuthority)
        return Result(PartyQuestAsyncSaveLifecycleStatus::Duplicate);

    if (m_state == State::CompletedSuccess || m_state == State::CompletedFailure)
        return Result(PartyQuestAsyncSaveLifecycleStatus::Duplicate);

    if (m_state != State::Active)
        return Result(PartyQuestAsyncSaveLifecycleStatus::Stale);

    m_state = aOutcome == PartyQuestAsyncSavePhysicalOutcome::Succeeded ?
        State::CompletedSuccess : State::CompletedFailure;

    return Result(m_admissionOpen ?
            PartyQuestAsyncSaveLifecycleStatus::CompletionAccepted :
            PartyQuestAsyncSaveLifecycleStatus::AdmissionClosed);
}

PartyQuestAsyncSaveLifecycleResult
PartyQuestAsyncSaveLifecycle::CloseAdmission() noexcept
{
    if (!m_admissionOpen)
        return Result(PartyQuestAsyncSaveLifecycleStatus::Duplicate);

    m_admissionOpen = false;
    return Result(PartyQuestAsyncSaveLifecycleStatus::AdmissionClosed);
}

PartyQuestAsyncSaveLifecycleResult
PartyQuestAsyncSaveLifecycle::ObserveCancelRequested(
    const PartyQuestAsyncSaveRequestIdentity& acIdentity) noexcept
{
    if (!acIdentity.IsValid())
        return Result(PartyQuestAsyncSaveLifecycleStatus::InvalidIdentity);
    if (!Matches(acIdentity))
        return Result(PartyQuestAsyncSaveLifecycleStatus::Stale);
    if (!HasActiveRequest())
        return Result(PartyQuestAsyncSaveLifecycleStatus::Duplicate);
    if (m_admissionOpen)
        return Result(PartyQuestAsyncSaveLifecycleStatus::Stale);
    if (m_cancelRequested)
        return Result(PartyQuestAsyncSaveLifecycleStatus::Duplicate);

    m_cancelRequested = true;
    return Result(PartyQuestAsyncSaveLifecycleStatus::AdmissionClosed);
}

PartyQuestAsyncSaveLifecycleResult PartyQuestAsyncSaveLifecycle::ObserveRetirement(
    const PartyQuestAsyncSaveRequestIdentity& acIdentity) noexcept
{
    if (!acIdentity.IsValid())
        return Result(PartyQuestAsyncSaveLifecycleStatus::InvalidIdentity);
    if (!Matches(acIdentity))
        return Result(PartyQuestAsyncSaveLifecycleStatus::Stale);

    if (m_state == State::RetiredAuthorized ||
        m_state == State::RetiredWithoutAuthority)
        return Result(PartyQuestAsyncSaveLifecycleStatus::Duplicate);

    const bool authorize = m_admissionOpen &&
        m_state == State::CompletedSuccess;
    m_state = authorize ? State::RetiredAuthorized :
                          State::RetiredWithoutAuthority;
    m_cancelRequested = false;
    return Result(authorize ?
            PartyQuestAsyncSaveLifecycleStatus::RetiredAuthorized :
            PartyQuestAsyncSaveLifecycleStatus::RetiredWithoutAuthority,
        authorize);
}

PartyQuestAsyncSaveLifecycleResult
PartyQuestAsyncSaveLifecycle::Current() const noexcept
{
    switch (m_state)
    {
    case State::Inactive:
        return Result(PartyQuestAsyncSaveLifecycleStatus::Inactive);
    case State::Active:
        return Result(m_admissionOpen ?
                PartyQuestAsyncSaveLifecycleStatus::Active :
                PartyQuestAsyncSaveLifecycleStatus::AdmissionClosed);
    case State::CompletedSuccess:
    case State::CompletedFailure:
        return Result(m_admissionOpen ?
                PartyQuestAsyncSaveLifecycleStatus::CompletionAccepted :
                PartyQuestAsyncSaveLifecycleStatus::AdmissionClosed);
    case State::RetiredAuthorized:
        return Result(PartyQuestAsyncSaveLifecycleStatus::RetiredAuthorized);
    case State::RetiredWithoutAuthority:
        return Result(
            PartyQuestAsyncSaveLifecycleStatus::RetiredWithoutAuthority);
    }

    return Result(PartyQuestAsyncSaveLifecycleStatus::Inactive);
}

PartyQuestAsyncSaveLifecycleResult PartyQuestAsyncSaveLifecycle::Result(
    PartyQuestAsyncSaveLifecycleStatus aStatus,
    bool aConsumptionAuthorized) const noexcept
{
    const bool active = HasActiveRequest();
    return {
        aStatus,
        active && !m_admissionOpen && !m_cancelRequested,
        active,
        !active,
        aConsumptionAuthorized && m_admissionOpen
    };
}

bool PartyQuestAsyncSaveLifecycle::Matches(
    const PartyQuestAsyncSaveRequestIdentity& acIdentity) const noexcept
{
    return m_identity.has_value() && *m_identity == acIdentity;
}

bool PartyQuestAsyncSaveLifecycle::HasActiveRequest() const noexcept
{
    return m_state == State::Active ||
        m_state == State::CompletedSuccess ||
        m_state == State::CompletedFailure;
}
