#include <Games/Skyrim/PartyQuestSkyrimNativeLoadBridgeProvider.h>

#include <cstddef>
#include <cstring>

namespace
{
class ExclusiveSrwLock final
{
public:
    explicit ExclusiveSrwLock(SRWLOCK& aLock) noexcept
        : m_lock(aLock)
    {
        ::AcquireSRWLockExclusive(&m_lock);
    }

    ~ExclusiveSrwLock() noexcept
    {
        ::ReleaseSRWLockExclusive(&m_lock);
    }

    ExclusiveSrwLock(const ExclusiveSrwLock&) = delete;
    ExclusiveSrwLock& operator=(const ExclusiveSrwLock&) = delete;

private:
    SRWLOCK& m_lock;
};

template <class T>
[[nodiscard]] bool CopyFromForeign(
    const T* apSource,
    T& aDestination) noexcept
{
    if (!apSource)
        return false;

    __try
    {
        std::memcpy(&aDestination, apSource, sizeof(T));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        aDestination = {};
        return false;
    }
}

template <class T>
[[nodiscard]] bool CopyToForeign(
    T* apDestination,
    const T& acSource) noexcept
{
    if (!apDestination)
        return false;

    __try
    {
        std::memcpy(apDestination, &acSource, sizeof(T));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

template <class T>
[[nodiscard]] bool PreflightOutput(T* apDestination) noexcept
{
    const T zero{};
    return CopyToForeign(apDestination, zero);
}

[[nodiscard]] uint32_t Raw(
    PartyQuestNativeLoadBridgeStatus aStatus) noexcept
{
    return static_cast<uint32_t>(aStatus);
}

[[nodiscard]] uint32_t Raw(
    PartyQuestNativeLoadBridgeDescriptorResult aResult) noexcept
{
    return static_cast<uint32_t>(aResult);
}
} // namespace

PartyQuestSkyrimNativeLoadBridgeProvider&
PartyQuestSkyrimNativeLoadBridgeProvider::GetProcessProvider() noexcept
{
    static PartyQuestSkyrimNativeLoadBridgeProvider s_provider;
    return s_provider;
}

bool PartyQuestSkyrimNativeLoadBridgeProvider::PublishReady(
    uint64_t aRuntimeFingerprint) noexcept
{
    ExclusiveSrwLock lock(m_lock);
    return m_adapter.PublishReady(aRuntimeFingerprint);
}

void PartyQuestSkyrimNativeLoadBridgeProvider::Poison() noexcept
{
    ExclusiveSrwLock lock(m_lock);
    PoisonLocked();
}

PartyQuestNativeLoadBridgeDescriptorResult
PartyQuestSkyrimNativeLoadBridgeProvider::GetDescriptor(
    PartyQuestNativeLoadBridgeDescriptorV1& aDescriptor) noexcept
{
    ExclusiveSrwLock lock(m_lock);
    return m_adapter.GetDescriptor(aDescriptor);
}

PartyQuestNativeLoadBridgeStatus
PartyQuestSkyrimNativeLoadBridgeProvider::Reserve(
    const PartyQuestNativeLoadBridgeReserveRequestV1& acRequest,
    PartyQuestNativeLoadBridgeReservationV1& aReservation) noexcept
{
    ExclusiveSrwLock lock(m_lock);

    const auto status = m_adapter.Reserve(acRequest, aReservation);
    if (status == PartyQuestNativeLoadBridgeStatus::Reserved)
    {
        if (aReservation.AttemptNonce == 0u ||
            (m_activeAttemptNonce != 0u &&
             m_activeAttemptNonce != aReservation.AttemptNonce))
        {
            aReservation = {};
            PoisonLocked();
            return PartyQuestNativeLoadBridgeStatus::InternalFailure;
        }

        m_activeAttemptNonce = aReservation.AttemptNonce;
    }

    return status;
}

PartyQuestNativeLoadBridgeStatus
PartyQuestSkyrimNativeLoadBridgeProvider::Cancel(
    uint64_t aAttemptNonce) noexcept
{
    ExclusiveSrwLock lock(m_lock);
    const auto status = m_adapter.Cancel(aAttemptNonce);
    ClearActiveNonceIfTerminalLocked(status);
    return status;
}

PartyQuestNativeLoadBridgeStatus
PartyQuestSkyrimNativeLoadBridgeProvider::Poll(
    uint64_t aAttemptNonce,
    PartyQuestNativeLoadBridgeCompletionV1& aCompletion) noexcept
{
    ExclusiveSrwLock lock(m_lock);
    return m_adapter.Poll(aAttemptNonce, aCompletion);
}

PartyQuestNativeLoadBridgeStatus
PartyQuestSkyrimNativeLoadBridgeProvider::Retire(
    uint64_t aAttemptNonce) noexcept
{
    ExclusiveSrwLock lock(m_lock);
    const auto status = m_adapter.Retire(aAttemptNonce);
    ClearActiveNonceIfTerminalLocked(status);
    return status;
}

PartyQuestSkyrimNativeLoadBridgeHookClaimResult
PartyQuestSkyrimNativeLoadBridgeProvider::ClaimReserved(
    const PartyQuestNativeLoadIdentity& acActualIdentity) noexcept
{
    ExclusiveSrwLock lock(m_lock);

    PartyQuestSkyrimNativeLoadBridgeHookClaimResult result;
    result.AttemptNonce = m_activeAttemptNonce;
    if (m_activeAttemptNonce == 0u)
    {
        result.Status = PartyQuestNativeLoadBridgeStatus::InvalidState;
        return result;
    }

    result.Status =
        m_adapter.Claim(m_activeAttemptNonce, acActualIdentity);
    if (result.Status != PartyQuestNativeLoadBridgeStatus::Pending)
        result.AttemptNonce = 0u;
    return result;
}

PartyQuestNativeLoadBridgeStatus
PartyQuestSkyrimNativeLoadBridgeProvider::MarkTargetEntered(
    uint64_t aAttemptNonce) noexcept
{
    ExclusiveSrwLock lock(m_lock);
    if (aAttemptNonce == 0u || aAttemptNonce != m_activeAttemptNonce)
        return PartyQuestNativeLoadBridgeStatus::NonceMismatch;
    return m_adapter.MarkTargetEntered(aAttemptNonce);
}

PartyQuestNativeLoadBridgeStatus
PartyQuestSkyrimNativeLoadBridgeProvider::CompleteTarget(
    uint64_t aAttemptNonce,
    bool aResult) noexcept
{
    ExclusiveSrwLock lock(m_lock);
    if (aAttemptNonce == 0u || aAttemptNonce != m_activeAttemptNonce)
        return PartyQuestNativeLoadBridgeStatus::NonceMismatch;
    return m_adapter.Complete(aAttemptNonce, aResult);
}

PartyQuestNativeLoadBridgeAdapterState
PartyQuestSkyrimNativeLoadBridgeProvider::GetState() noexcept
{
    ExclusiveSrwLock lock(m_lock);
    return m_adapter.GetState();
}

uint64_t
PartyQuestSkyrimNativeLoadBridgeProvider::GetActiveAttemptNonce() noexcept
{
    ExclusiveSrwLock lock(m_lock);
    return m_activeAttemptNonce;
}

void PartyQuestSkyrimNativeLoadBridgeProvider::PoisonLocked() noexcept
{
    m_adapter.Poison();
}

void PartyQuestSkyrimNativeLoadBridgeProvider::
    ClearActiveNonceIfTerminalLocked(
        PartyQuestNativeLoadBridgeStatus aStatus) noexcept
{
    if (aStatus == PartyQuestNativeLoadBridgeStatus::Cancelled ||
        aStatus == PartyQuestNativeLoadBridgeStatus::Retired)
    {
        m_activeAttemptNonce = 0u;
    }
}

uint32_t PartyQuestSkyrimNativeLoadBridgeProviderAbi::GetDescriptor(
    PartyQuestSkyrimNativeLoadBridgeProvider& aProvider,
    PartyQuestNativeLoadBridgeDescriptorV1* apDescriptor,
    uint32_t aDescriptorSize) noexcept
{
    if (!apDescriptor)
        return Raw(PartyQuestNativeLoadBridgeDescriptorResult::Unavailable);
    if (aDescriptorSize != sizeof(PartyQuestNativeLoadBridgeDescriptorV1))
        return Raw(PartyQuestNativeLoadBridgeDescriptorResult::Unavailable);
    if (!PreflightOutput(apDescriptor))
        return Raw(PartyQuestNativeLoadBridgeDescriptorResult::Unavailable);

    PartyQuestNativeLoadBridgeDescriptorV1 descriptor{};
    const auto result = aProvider.GetDescriptor(descriptor);
    if (!CopyToForeign(apDescriptor, descriptor))
        return Raw(PartyQuestNativeLoadBridgeDescriptorResult::Unavailable);
    return Raw(result);
}

uint32_t PartyQuestSkyrimNativeLoadBridgeProviderAbi::Reserve(
    PartyQuestSkyrimNativeLoadBridgeProvider& aProvider,
    const PartyQuestNativeLoadBridgeReserveRequestV1* apRequest,
    uint32_t aRequestSize,
    PartyQuestNativeLoadBridgeReservationV1* apReservation,
    uint32_t aReservationSize) noexcept
{
    if (!apRequest || !apReservation)
        return Raw(PartyQuestNativeLoadBridgeStatus::InvalidArgument);
    if (aRequestSize != sizeof(PartyQuestNativeLoadBridgeReserveRequestV1) ||
        aReservationSize != sizeof(PartyQuestNativeLoadBridgeReservationV1))
    {
        return Raw(PartyQuestNativeLoadBridgeStatus::InvalidStructSize);
    }

    PartyQuestNativeLoadBridgeReserveRequestV1 request{};
    if (!CopyFromForeign(apRequest, request) ||
        !PreflightOutput(apReservation))
    {
        return Raw(PartyQuestNativeLoadBridgeStatus::InvalidArgument);
    }

    PartyQuestNativeLoadBridgeReservationV1 reservation{};
    const auto status = aProvider.Reserve(request, reservation);
    if (!CopyToForeign(apReservation, reservation))
    {
        if (status == PartyQuestNativeLoadBridgeStatus::Reserved)
            aProvider.Poison();
        return Raw(PartyQuestNativeLoadBridgeStatus::InternalFailure);
    }

    return Raw(status);
}

uint32_t PartyQuestSkyrimNativeLoadBridgeProviderAbi::Cancel(
    PartyQuestSkyrimNativeLoadBridgeProvider& aProvider,
    uint64_t aAttemptNonce) noexcept
{
    return Raw(aProvider.Cancel(aAttemptNonce));
}

uint32_t PartyQuestSkyrimNativeLoadBridgeProviderAbi::Poll(
    PartyQuestSkyrimNativeLoadBridgeProvider& aProvider,
    uint64_t aAttemptNonce,
    PartyQuestNativeLoadBridgeCompletionV1* apCompletion,
    uint32_t aCompletionSize) noexcept
{
    if (!apCompletion)
        return Raw(PartyQuestNativeLoadBridgeStatus::InvalidArgument);
    if (aCompletionSize != sizeof(PartyQuestNativeLoadBridgeCompletionV1))
        return Raw(PartyQuestNativeLoadBridgeStatus::InvalidStructSize);
    if (!PreflightOutput(apCompletion))
        return Raw(PartyQuestNativeLoadBridgeStatus::InvalidArgument);

    PartyQuestNativeLoadBridgeCompletionV1 completion{};
    const auto status = aProvider.Poll(aAttemptNonce, completion);
    if (!CopyToForeign(apCompletion, completion))
    {
        aProvider.Poison();
        return Raw(PartyQuestNativeLoadBridgeStatus::InternalFailure);
    }

    return Raw(status);
}

uint32_t PartyQuestSkyrimNativeLoadBridgeProviderAbi::Retire(
    PartyQuestSkyrimNativeLoadBridgeProvider& aProvider,
    uint64_t aAttemptNonce) noexcept
{
    return Raw(aProvider.Retire(aAttemptNonce));
}

extern "C" uint32_t PartyQuestProcessNativeLoadBridge_GetDescriptor(
    PartyQuestNativeLoadBridgeDescriptorV1* apDescriptor,
    uint32_t aDescriptorSize)
{
    return PartyQuestSkyrimNativeLoadBridgeProviderAbi::GetDescriptor(
        PartyQuestSkyrimNativeLoadBridgeProvider::GetProcessProvider(),
        apDescriptor,
        aDescriptorSize);
}

extern "C" uint32_t PartyQuestProcessNativeLoadBridge_Reserve(
    const PartyQuestNativeLoadBridgeReserveRequestV1* apRequest,
    uint32_t aRequestSize,
    PartyQuestNativeLoadBridgeReservationV1* apReservation,
    uint32_t aReservationSize)
{
    return PartyQuestSkyrimNativeLoadBridgeProviderAbi::Reserve(
        PartyQuestSkyrimNativeLoadBridgeProvider::GetProcessProvider(),
        apRequest,
        aRequestSize,
        apReservation,
        aReservationSize);
}

extern "C" uint32_t PartyQuestProcessNativeLoadBridge_Cancel(
    uint64_t aAttemptNonce)
{
    return PartyQuestSkyrimNativeLoadBridgeProviderAbi::Cancel(
        PartyQuestSkyrimNativeLoadBridgeProvider::GetProcessProvider(),
        aAttemptNonce);
}

extern "C" uint32_t PartyQuestProcessNativeLoadBridge_Poll(
    uint64_t aAttemptNonce,
    PartyQuestNativeLoadBridgeCompletionV1* apCompletion,
    uint32_t aCompletionSize)
{
    return PartyQuestSkyrimNativeLoadBridgeProviderAbi::Poll(
        PartyQuestSkyrimNativeLoadBridgeProvider::GetProcessProvider(),
        aAttemptNonce,
        apCompletion,
        aCompletionSize);
}

extern "C" uint32_t PartyQuestProcessNativeLoadBridge_Retire(
    uint64_t aAttemptNonce)
{
    return PartyQuestSkyrimNativeLoadBridgeProviderAbi::Retire(
        PartyQuestSkyrimNativeLoadBridgeProvider::GetProcessProvider(),
        aAttemptNonce);
}
