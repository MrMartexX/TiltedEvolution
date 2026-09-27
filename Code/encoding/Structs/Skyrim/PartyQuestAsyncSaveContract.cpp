#include <Structs/Skyrim/PartyQuestAsyncSaveContract.h>

#include <array>
#include <cstdio>
#include <utility>

namespace
{
std::string ExpectedSaveName(uint64_t aTransactionId, uint64_t aTargetWorldRevision, uint64_t aAttemptNonce)
{
    std::array<char, 96> buffer{};
    const int written = std::snprintf(
        buffer.data(), buffer.size(), "STR_PreRepair_T%016llX_R%016llX_A%016llX", static_cast<unsigned long long>(aTransactionId),
        static_cast<unsigned long long>(aTargetWorldRevision), static_cast<unsigned long long>(aAttemptNonce));
    if (written <= 0 || static_cast<size_t>(written) >= buffer.size())
        return {};
    return {buffer.data(), static_cast<size_t>(written)};
}

bool IsKnownArtifact(PartyQuestAsyncSaveArtifact aArtifact) noexcept
{
    return aArtifact == PartyQuestAsyncSaveArtifact::SkyrimEss || aArtifact == PartyQuestAsyncSaveArtifact::SkseCosave;
}

bool IsKnownOutcome(PartyQuestAsyncSaveArtifactOutcome aOutcome) noexcept
{
    return aOutcome == PartyQuestAsyncSaveArtifactOutcome::ClosedSuccess || aOutcome == PartyQuestAsyncSaveArtifactOutcome::Failed;
}

bool IsKnownPublicationOutcome(PartyQuestAsyncSavePublicationOutcome aOutcome) noexcept
{
    return aOutcome == PartyQuestAsyncSavePublicationOutcome::PublishedSuccess || aOutcome == PartyQuestAsyncSavePublicationOutcome::Failed;
}
} // namespace

bool PartyQuestAsyncSaveRequestIdentity::IsValid() const noexcept
{
    try
    {
        return CampaignId.IsValid() && PlayerProfileId.IsValid() && RuntimeGeneration != 0 && TransactionId != 0 && TargetWorldRevision != 0 && CaptureEpochId != 0 &&
               AttemptNonce != 0 && SaveName == ExpectedSaveName(TransactionId, TargetWorldRevision, AttemptNonce);
    }
    catch (...)
    {
        return false;
    }
}

PartyQuestAsyncSaveCompletion::PartyQuestAsyncSaveCompletion(PartyQuestAsyncSaveCompletion&& aOther) noexcept
    : m_identity(std::move(aOther.m_identity))
    , m_nonce(aOther.m_nonce)
{
    aOther.m_nonce = 0;
}

PartyQuestAsyncSaveCompletion& PartyQuestAsyncSaveCompletion::operator=(PartyQuestAsyncSaveCompletion&& aOther) noexcept
{
    if (this != &aOther)
    {
        m_identity = std::move(aOther.m_identity);
        m_nonce = aOther.m_nonce;
        aOther.m_nonce = 0;
    }
    return *this;
}

bool PartyQuestAsyncSaveCompletion::Matches(const PartyQuestAsyncSaveRequestIdentity& acIdentity) const noexcept
{
    return IsValid() && m_identity == acIdentity;
}

PartyQuestAsyncSaveContractResult PartyQuestAsyncSaveContract::Result(PartyQuestAsyncSaveContractStatus aStatus) const noexcept
{
    PartyQuestAsyncSaveContractResult result;
    result.Status = aStatus;
    result.SkyrimEssClosed = m_mainClosed;
    result.SkseCosaveClosed = m_cosaveClosed;
    result.SkyrimEssPublished = m_mainPublished;
    result.SkseCosavePublished = m_cosavePublished;
    result.CleanupRequired = m_identity.has_value() && (aStatus == PartyQuestAsyncSaveContractStatus::Failed || aStatus == PartyQuestAsyncSaveContractStatus::TimedOut ||
                                                        aStatus == PartyQuestAsyncSaveContractStatus::Cancelled || aStatus == PartyQuestAsyncSaveContractStatus::InvalidClock);
    return result;
}

PartyQuestAsyncSaveContractResult PartyQuestAsyncSaveContract::Fail(PartyQuestAsyncSaveContractStatus aStatus) noexcept
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

PartyQuestAsyncSaveContractResult
PartyQuestAsyncSaveContract::Begin(PartyQuestAsyncSaveRequestIdentity aIdentity, uint64_t aNowMs, bool aMainPathExists, bool aCosavePathExists) noexcept
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
        m_mainPublished = false;
        m_cosavePublished = false;
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
    const PartyQuestAsyncSaveRequestIdentity& acIdentity, PartyQuestAsyncSaveArtifact aArtifact, PartyQuestAsyncSaveArtifactOutcome aOutcome, uint64_t aNowMs) noexcept
{
    if (!m_identity || m_status == PartyQuestAsyncSaveContractStatus::Inactive)
        return Result(PartyQuestAsyncSaveContractStatus::Stale);
    if (*m_identity != acIdentity)
        return Result(PartyQuestAsyncSaveContractStatus::Stale);
    if (!IsKnownArtifact(aArtifact) || !IsKnownOutcome(aOutcome))
    {
        if (m_status == PartyQuestAsyncSaveContractStatus::Pending ||
            m_status == PartyQuestAsyncSaveContractStatus::Complete)
            return Fail(PartyQuestAsyncSaveContractStatus::Failed);
        return Result(m_status);
    }
    if (m_status != PartyQuestAsyncSaveContractStatus::Pending)
    {
        if (m_status == PartyQuestAsyncSaveContractStatus::Complete)
            return aOutcome == PartyQuestAsyncSaveArtifactOutcome::Failed ?
                Fail(PartyQuestAsyncSaveContractStatus::Failed) :
                Result(PartyQuestAsyncSaveContractStatus::Duplicate);
        return Result(m_status);
    }
    if (!CheckClock(aNowMs))
        return Result(m_status);

    // The observer is an ABI boundary. Unknown discriminants must never alias
    // a known artifact or success result after an enum/version mismatch.
    if (aOutcome == PartyQuestAsyncSaveArtifactOutcome::Failed)
        return Fail(PartyQuestAsyncSaveContractStatus::Failed);

    bool& closed = aArtifact == PartyQuestAsyncSaveArtifact::SkyrimEss ? m_mainClosed : m_cosaveClosed;
    if (closed)
        return Result(PartyQuestAsyncSaveContractStatus::Duplicate);

    closed = true;
    return Result(PartyQuestAsyncSaveContractStatus::Pending);
}

PartyQuestAsyncSaveContractResult PartyQuestAsyncSaveContract::ObservePublication(
    const PartyQuestAsyncSaveRequestIdentity& acIdentity, PartyQuestAsyncSaveArtifact aArtifact, PartyQuestAsyncSavePublicationOutcome aOutcome, uint64_t aNowMs) noexcept
{
    if (!m_identity || m_status == PartyQuestAsyncSaveContractStatus::Inactive)
        return Result(PartyQuestAsyncSaveContractStatus::Stale);
    if (*m_identity != acIdentity)
        return Result(PartyQuestAsyncSaveContractStatus::Stale);
    if (!IsKnownArtifact(aArtifact) || !IsKnownPublicationOutcome(aOutcome))
    {
        if (m_status == PartyQuestAsyncSaveContractStatus::Pending ||
            m_status == PartyQuestAsyncSaveContractStatus::Complete)
            return Fail(PartyQuestAsyncSaveContractStatus::Failed);
        return Result(m_status);
    }
    if (m_status != PartyQuestAsyncSaveContractStatus::Pending)
    {
        if (m_status == PartyQuestAsyncSaveContractStatus::Complete)
            return aOutcome == PartyQuestAsyncSavePublicationOutcome::Failed ?
                Fail(PartyQuestAsyncSaveContractStatus::Failed) :
                Result(PartyQuestAsyncSaveContractStatus::Duplicate);
        return Result(m_status);
    }
    if (!CheckClock(aNowMs))
        return Result(m_status);

    if (aOutcome == PartyQuestAsyncSavePublicationOutcome::Failed)
        return Fail(PartyQuestAsyncSaveContractStatus::Failed);

    const bool closed = aArtifact == PartyQuestAsyncSaveArtifact::SkyrimEss ? m_mainClosed : m_cosaveClosed;
    if (!closed)
        return Fail(PartyQuestAsyncSaveContractStatus::Failed);

    bool& published = aArtifact == PartyQuestAsyncSaveArtifact::SkyrimEss ? m_mainPublished : m_cosavePublished;
    if (published)
        return Result(PartyQuestAsyncSaveContractStatus::Duplicate);

    published = true;
    if (!m_mainPublished || !m_cosavePublished)
        return Result(PartyQuestAsyncSaveContractStatus::Pending);

    PartyQuestAsyncSaveCompletion completion;
    try
    {
        completion.m_identity = *m_identity;
    }
    catch (...)
    {
        return Fail(PartyQuestAsyncSaveContractStatus::Failed);
    }
    completion.m_nonce = m_completionNonce;
    if (completion.m_nonce == 0)
        return Fail(PartyQuestAsyncSaveContractStatus::Failed);
    ++m_completionNonce;
    m_status = PartyQuestAsyncSaveContractStatus::Complete;
    PartyQuestAsyncSaveContractResult result = Result(m_status);
    result.Completion.emplace(std::move(completion));
    return result;
}

PartyQuestAsyncSaveContractResult PartyQuestAsyncSaveContract::Poll(uint64_t aNowMs) noexcept
{
    if (m_status != PartyQuestAsyncSaveContractStatus::Pending)
        return Result(m_status);
    if (!CheckClock(aNowMs))
        return Result(m_status);
    return Result(m_status);
}

PartyQuestAsyncSaveContractResult PartyQuestAsyncSaveContract::Cancel(const PartyQuestAsyncSaveRequestIdentity& acIdentity) noexcept
{
    if (!m_identity || *m_identity != acIdentity)
        return Result(PartyQuestAsyncSaveContractStatus::Stale);
    if (m_status == PartyQuestAsyncSaveContractStatus::Complete)
        return Result(PartyQuestAsyncSaveContractStatus::Duplicate);
    if (m_status != PartyQuestAsyncSaveContractStatus::Pending)
        return Result(m_status);
    return Fail(PartyQuestAsyncSaveContractStatus::Cancelled);
}

PartyQuestAsyncSaveContractResult PartyQuestAsyncSaveContract::Retire(const PartyQuestAsyncSaveRequestIdentity& acIdentity) noexcept
{
    if (!m_identity || *m_identity != acIdentity)
        return Result(PartyQuestAsyncSaveContractStatus::Stale);
    if (m_status == PartyQuestAsyncSaveContractStatus::Pending)
        return Result(m_status);

    m_identity.reset();
    m_startedAtMs = 0;
    m_lastNowMs = 0;
    m_mainClosed = false;
    m_cosaveClosed = false;
    m_mainPublished = false;
    m_cosavePublished = false;
    m_status = PartyQuestAsyncSaveContractStatus::Inactive;
    return Result(m_status);
}
