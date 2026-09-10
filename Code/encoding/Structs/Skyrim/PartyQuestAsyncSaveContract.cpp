#include <Structs/Skyrim/PartyQuestAsyncSaveContract.h>

#include <utility>

bool PartyQuestAsyncSaveRequestIdentity::IsValid() const noexcept
{
    return CampaignId.IsValid() && PlayerProfileId.IsValid() &&
        RuntimeGeneration != 0 && TransactionId != 0 &&
        TargetWorldRevision != 0 && CaptureEpochId != 0 &&
        AttemptNonce != 0 && !SaveName.empty() &&
        SaveName.find_first_of("/\\:") == std::string::npos;
}

PartyQuestAsyncSaveCompletion::PartyQuestAsyncSaveCompletion(
    PartyQuestAsyncSaveCompletion&& aOther) noexcept
    : m_identity(std::move(aOther.m_identity))
    , m_nonce(aOther.m_nonce)
{
    aOther.m_nonce = 0;
}

PartyQuestAsyncSaveCompletion& PartyQuestAsyncSaveCompletion::operator=(
    PartyQuestAsyncSaveCompletion&& aOther) noexcept
{
    if (this != &aOther)
    {
        m_identity = std::move(aOther.m_identity);
        m_nonce = aOther.m_nonce;
        aOther.m_nonce = 0;
    }
    return *this;
}

bool PartyQuestAsyncSaveCompletion::Matches(
    const PartyQuestAsyncSaveRequestIdentity& acIdentity) const noexcept
{
    return IsValid() && m_identity == acIdentity;
}

PartyQuestAsyncSaveContractResult PartyQuestAsyncSaveContract::Result(
    PartyQuestAsyncSaveContractStatus aStatus) const noexcept
{
    PartyQuestAsyncSaveContractResult result;
    result.Status = aStatus;
    result.SkyrimEssClosed = m_mainClosed;
    result.SkseCosaveClosed = m_cosaveClosed;
    result.CleanupRequired = m_identity.has_value() &&
        (aStatus == PartyQuestAsyncSaveContractStatus::Failed ||
         aStatus == PartyQuestAsyncSaveContractStatus::TimedOut ||
         aStatus == PartyQuestAsyncSaveContractStatus::Cancelled ||
         aStatus == PartyQuestAsyncSaveContractStatus::InvalidClock);
    return result;
}

PartyQuestAsyncSaveContractResult PartyQuestAsyncSaveContract::Fail(
    PartyQuestAsyncSaveContractStatus aStatus) noexcept
{
    m_status = aStatus;
    return Result(aStatus);
}

bool PartyQuestAsyncSaveContract::CheckClock(uint64_t aNowMs) noexcept
{
    if (aNowMs < m_lastNowMs)
    {
        m_status = PartyQuestAsyncSaveContractStatus::InvalidClock;
        return false;
    }
    m_lastNowMs = aNowMs;
    if (aNowMs - m_startedAtMs > kTimeoutMs)
    {
        m_status = PartyQuestAsyncSaveContractStatus::TimedOut;
        return false;
    }
    return true;
}

PartyQuestAsyncSaveContractResult PartyQuestAsyncSaveContract::Begin(
    PartyQuestAsyncSaveRequestIdentity aIdentity, uint64_t aNowMs,
    bool aMainPathExists, bool aCosavePathExists) noexcept
{
    try
    {
        if (m_status != PartyQuestAsyncSaveContractStatus::Inactive)
            return Result(PartyQuestAsyncSaveContractStatus::Busy);
        if (!aIdentity.IsValid())
            return Result(PartyQuestAsyncSaveContractStatus::InvalidIdentity);
        if (aMainPathExists || aCosavePathExists)
            return Result(PartyQuestAsyncSaveContractStatus::ExistingFileConflict);

        m_identity.emplace(std::move(aIdentity));
        m_startedAtMs = aNowMs;
        m_lastNowMs = aNowMs;
        m_mainClosed = false;
        m_cosaveClosed = false;
        m_status = PartyQuestAsyncSaveContractStatus::Pending;
        return Result(m_status);
    }
    catch (...)
    {
        m_identity.reset();
        m_status = PartyQuestAsyncSaveContractStatus::Inactive;
        return Result(PartyQuestAsyncSaveContractStatus::InvalidIdentity);
    }
}

PartyQuestAsyncSaveContractResult PartyQuestAsyncSaveContract::Observe(
    const PartyQuestAsyncSaveRequestIdentity& acIdentity,
    PartyQuestAsyncSaveArtifact aArtifact,
    PartyQuestAsyncSaveArtifactOutcome aOutcome,
    uint64_t aNowMs) noexcept
{
    if (!m_identity || m_status == PartyQuestAsyncSaveContractStatus::Inactive)
        return Result(PartyQuestAsyncSaveContractStatus::Stale);
    if (*m_identity != acIdentity)
        return Result(PartyQuestAsyncSaveContractStatus::Stale);
    if (m_status != PartyQuestAsyncSaveContractStatus::Pending)
        return Result(m_status == PartyQuestAsyncSaveContractStatus::Complete
                ? PartyQuestAsyncSaveContractStatus::Duplicate : m_status);
    if (!CheckClock(aNowMs))
        return Result(m_status);

    bool& closed = aArtifact == PartyQuestAsyncSaveArtifact::SkyrimEss
        ? m_mainClosed : m_cosaveClosed;
    if (closed)
        return Result(PartyQuestAsyncSaveContractStatus::Duplicate);
    if (aOutcome == PartyQuestAsyncSaveArtifactOutcome::Failed)
        return Fail(PartyQuestAsyncSaveContractStatus::Failed);

    closed = true;
    if (!m_mainClosed || !m_cosaveClosed)
        return Result(PartyQuestAsyncSaveContractStatus::Pending);

    m_status = PartyQuestAsyncSaveContractStatus::Complete;
    PartyQuestAsyncSaveContractResult result = Result(m_status);
    PartyQuestAsyncSaveCompletion completion;
    completion.m_identity = *m_identity;
    completion.m_nonce = m_completionNonce++;
    if (completion.m_nonce == 0)
        return Fail(PartyQuestAsyncSaveContractStatus::Failed);
    result.Completion.emplace(std::move(completion));
    return result;
}

PartyQuestAsyncSaveContractResult PartyQuestAsyncSaveContract::Poll(
    uint64_t aNowMs) noexcept
{
    if (m_status != PartyQuestAsyncSaveContractStatus::Pending)
        return Result(m_status);
    if (!CheckClock(aNowMs))
        return Result(m_status);
    return Result(m_status);
}

PartyQuestAsyncSaveContractResult PartyQuestAsyncSaveContract::Cancel(
    const PartyQuestAsyncSaveRequestIdentity& acIdentity) noexcept
{
    if (!m_identity || *m_identity != acIdentity)
        return Result(PartyQuestAsyncSaveContractStatus::Stale);
    if (m_status == PartyQuestAsyncSaveContractStatus::Complete)
        return Result(PartyQuestAsyncSaveContractStatus::Duplicate);
    if (m_status != PartyQuestAsyncSaveContractStatus::Pending)
        return Result(m_status);
    return Fail(PartyQuestAsyncSaveContractStatus::Cancelled);
}

void PartyQuestAsyncSaveContract::Retire() noexcept
{
    m_identity.reset();
    m_startedAtMs = 0;
    m_lastNowMs = 0;
    m_mainClosed = false;
    m_cosaveClosed = false;
    m_status = PartyQuestAsyncSaveContractStatus::Inactive;
}
