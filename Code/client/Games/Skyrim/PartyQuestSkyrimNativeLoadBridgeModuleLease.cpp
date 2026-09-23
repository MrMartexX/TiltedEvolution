#include <Games/Skyrim/PartyQuestSkyrimNativeLoadBridgeModuleLease.h>

#include <Windows.h>

#include <cstddef>
#include <cstring>

namespace
{
class ScopedModuleReference final
{
public:
    explicit ScopedModuleReference(HMODULE aModule) noexcept
        : m_module(aModule)
    {
    }

    ~ScopedModuleReference() noexcept
    {
        if (m_module)
            ::FreeLibrary(m_module);
    }

    ScopedModuleReference(const ScopedModuleReference&) = delete;
    ScopedModuleReference& operator=(const ScopedModuleReference&) = delete;

private:
    HMODULE m_module{};
};

template <class T>
[[nodiscard]] bool GetModuleForExport(
    T apExport,
    DWORD aFlags,
    HMODULE& aModule) noexcept
{
    aModule = nullptr;
    if (!apExport)
        return false;

    return ::GetModuleHandleExW(
               GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | aFlags,
               reinterpret_cast<LPCWSTR>(apExport),
               &aModule) != FALSE &&
        aModule != nullptr;
}

template <class T>
[[nodiscard]] bool ExportBelongsToModule(
    HMODULE aModule,
    T apExport) noexcept
{
    if (!aModule || !apExport)
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (::VirtualQuery(
            reinterpret_cast<LPCVOID>(apExport),
            &memory,
            sizeof(memory)) != sizeof(memory) ||
        memory.AllocationBase != aModule ||
        memory.State != MEM_COMMIT ||
        memory.Type != MEM_IMAGE)
    {
        return false;
    }

    const DWORD protection = memory.Protect & 0xFFu;
    return protection == PAGE_EXECUTE ||
        protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE ||
        protection == PAGE_EXECUTE_WRITECOPY;
}

[[nodiscard]] bool AreZero(
    const uint8_t* apBytes,
    size_t aSize) noexcept
{
    if (!apBytes && aSize != 0u)
        return false;

    for (size_t index = 0u; index < aSize; ++index)
    {
        if (apBytes[index] != 0u)
            return false;
    }
    return true;
}

[[nodiscard]] bool IsZeroIdentity(
    const PartyQuestNativeLoadBridgeIdentityV1& acIdentity) noexcept
{
    return acIdentity.Length == 0u &&
        acIdentity.Reserved0 == 0u &&
        AreZero(acIdentity.Bytes, sizeof(acIdentity.Bytes));
}

[[nodiscard]] bool IsZeroReserveRequest(
    const PartyQuestNativeLoadBridgeReserveRequestV1& acRequest) noexcept
{
    return acRequest.AbiVersion == 0u &&
        acRequest.StructSize == 0u &&
        IsZeroIdentity(acRequest.Identity);
}

[[nodiscard]] bool IdentityEquals(
    const PartyQuestNativeLoadBridgeIdentityV1& acLeft,
    const PartyQuestNativeLoadBridgeIdentityV1& acRight) noexcept
{
    return PartyQuestNativeLoadBridgePolicy::IsValidIdentity(acLeft) &&
        PartyQuestNativeLoadBridgePolicy::IsValidIdentity(acRight) &&
        acLeft.Length == acRight.Length &&
        std::memcmp(
            acLeft.Bytes,
            acRight.Bytes,
            acLeft.Length) == 0;
}

[[nodiscard]] bool IsAllowedStatus(
    PartyQuestNativeLoadBridgeOwnerEffectKind aKind,
    uint32_t aRawStatus) noexcept
{
    if (!PartyQuestNativeLoadBridgePolicy::IsKnownStatus(aRawStatus))
        return false;

    const auto status =
        static_cast<PartyQuestNativeLoadBridgeStatus>(aRawStatus);
    switch (aKind)
    {
    case PartyQuestNativeLoadBridgeOwnerEffectKind::Reserve:
        return status == PartyQuestNativeLoadBridgeStatus::Reserved;

    case PartyQuestNativeLoadBridgeOwnerEffectKind::Cancel:
        return status == PartyQuestNativeLoadBridgeStatus::Cancelled ||
            status == PartyQuestNativeLoadBridgeStatus::InvalidState;

    case PartyQuestNativeLoadBridgeOwnerEffectKind::Poll:
        return status == PartyQuestNativeLoadBridgeStatus::Pending ||
            status ==
                PartyQuestNativeLoadBridgeStatus::CompletionAvailable;

    case PartyQuestNativeLoadBridgeOwnerEffectKind::Retire:
        return status == PartyQuestNativeLoadBridgeStatus::Retired;

    case PartyQuestNativeLoadBridgeOwnerEffectKind::None:
        return false;
    }

    return false;
}

[[nodiscard]] bool CallReserveCpp(
    PartyQuestNativeLoadBridgeReserveExport apReserve,
    const PartyQuestNativeLoadBridgeReserveRequestV1& acRequest,
    PartyQuestNativeLoadBridgeReservationV1& aReservation,
    uint32_t& aRawStatus) noexcept
{
    try
    {
        aRawStatus = apReserve(
            &acRequest,
            static_cast<uint32_t>(sizeof(acRequest)),
            &aReservation,
            static_cast<uint32_t>(sizeof(aReservation)));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

[[nodiscard]] bool CallReserveSafely(
    PartyQuestNativeLoadBridgeReserveExport apReserve,
    const PartyQuestNativeLoadBridgeReserveRequestV1& acRequest,
    PartyQuestNativeLoadBridgeReservationV1& aReservation,
    uint32_t& aRawStatus) noexcept
{
    __try
    {
        return CallReserveCpp(
            apReserve, acRequest, aReservation, aRawStatus);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

template <class TExport>
[[nodiscard]] bool CallNonceCpp(
    TExport apExport,
    uint64_t aAttemptNonce,
    uint32_t& aRawStatus) noexcept
{
    try
    {
        aRawStatus = apExport(aAttemptNonce);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

template <class TExport>
[[nodiscard]] bool CallNonceSafely(
    TExport apExport,
    uint64_t aAttemptNonce,
    uint32_t& aRawStatus) noexcept
{
    __try
    {
        return CallNonceCpp(
            apExport, aAttemptNonce, aRawStatus);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

[[nodiscard]] bool CallPollCpp(
    PartyQuestNativeLoadBridgePollExport apPoll,
    uint64_t aAttemptNonce,
    PartyQuestNativeLoadBridgeCompletionV1& aCompletion,
    uint32_t& aRawStatus) noexcept
{
    try
    {
        aRawStatus = apPoll(
            aAttemptNonce,
            &aCompletion,
            static_cast<uint32_t>(sizeof(aCompletion)));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

[[nodiscard]] bool CallPollSafely(
    PartyQuestNativeLoadBridgePollExport apPoll,
    uint64_t aAttemptNonce,
    PartyQuestNativeLoadBridgeCompletionV1& aCompletion,
    uint32_t& aRawStatus) noexcept
{
    __try
    {
        return CallPollCpp(
            apPoll, aAttemptNonce, aCompletion, aRawStatus);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
} // namespace

PartyQuestSkyrimNativeLoadBridgeModuleLease::
    PartyQuestSkyrimNativeLoadBridgeModuleLease(
        PartyQuestSkyrimNativeLoadBridgeModuleLease&& aOther) noexcept
    : m_module(aOther.m_module)
    , m_reserve(aOther.m_reserve)
    , m_cancel(aOther.m_cancel)
    , m_poll(aOther.m_poll)
    , m_retire(aOther.m_retire)
    , m_boundGeneration(aOther.m_boundGeneration)
    , m_runtimeFingerprint(aOther.m_runtimeFingerprint)
    , m_poisoned(aOther.m_poisoned)
{
    aOther.ResetMovedFrom();
}

PartyQuestSkyrimNativeLoadBridgeModuleLease
PartyQuestSkyrimNativeLoadBridgeModuleLease::CreateAuthenticated(
    void* apExpectedModule,
    uint64_t aBoundGeneration,
    uint64_t aRuntimeFingerprint,
    PartyQuestNativeLoadBridgeGetDescriptorExport apGetDescriptor,
    PartyQuestNativeLoadBridgeReserveExport apReserve,
    PartyQuestNativeLoadBridgeCancelExport apCancel,
    PartyQuestNativeLoadBridgePollExport apPoll,
    PartyQuestNativeLoadBridgeRetireExport apRetire,
    PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus&
        aStatus) noexcept
{
    PartyQuestSkyrimNativeLoadBridgeModuleLease result;
    aStatus =
        PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus::
            InvalidArgument;

    if (!apExpectedModule ||
        aBoundGeneration == 0u ||
        aRuntimeFingerprint == 0u ||
        !apGetDescriptor ||
        !apReserve ||
        !apCancel ||
        !apPoll ||
        !apRetire)
    {
        return result;
    }

    const auto expectedModule =
        static_cast<HMODULE>(apExpectedModule);

    HMODULE referencedModule = nullptr;
    if (!GetModuleForExport(
            apGetDescriptor,
            0u,
            referencedModule))
    {
        aStatus =
            PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus::
                ModuleReferenceFailed;
        return result;
    }
    const ScopedModuleReference moduleReference(referencedModule);

    if (referencedModule != expectedModule ||
        !ExportBelongsToModule(expectedModule, apGetDescriptor) ||
        !ExportBelongsToModule(expectedModule, apReserve) ||
        !ExportBelongsToModule(expectedModule, apCancel) ||
        !ExportBelongsToModule(expectedModule, apPoll) ||
        !ExportBelongsToModule(expectedModule, apRetire))
    {
        aStatus =
            PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus::
                ExportModuleMismatch;
        return result;
    }

    HMODULE pinnedModule = nullptr;
    if (!GetModuleForExport(
            apGetDescriptor,
            GET_MODULE_HANDLE_EX_FLAG_PIN,
            pinnedModule) ||
        pinnedModule != expectedModule)
    {
        aStatus =
            PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus::
                ModulePinFailed;
        return result;
    }

    result.m_module = pinnedModule;
    result.m_reserve = apReserve;
    result.m_cancel = apCancel;
    result.m_poll = apPoll;
    result.m_retire = apRetire;
    result.m_boundGeneration = aBoundGeneration;
    result.m_runtimeFingerprint = aRuntimeFingerprint;
    aStatus =
        PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus::Ready;
    return result;
}

PartyQuestNativeLoadBridgeOwnerForeignOutcome
PartyQuestSkyrimNativeLoadBridgeModuleLease::Execute(
    const PartyQuestNativeLoadBridgeCallCapability& acCapability,
    const PartyQuestNativeLoadBridgeOwnerEffect& acEffect) noexcept
{
    if (!IsCallable() || !Matches(acCapability, acEffect))
        return FailUnknown(acEffect);

    PartyQuestNativeLoadBridgeOwnerForeignOutcome outcome{};
    outcome.EffectKind = acEffect.Kind;
    outcome.Disposition =
        PartyQuestNativeLoadBridgeOwnerForeignDisposition::Returned;
    outcome.EffectSequence = acEffect.Sequence;

    uint32_t rawStatus = 0u;
    switch (acEffect.Kind)
    {
    case PartyQuestNativeLoadBridgeOwnerEffectKind::Reserve:
    {
        PartyQuestNativeLoadBridgeReservationV1 reservation{};
        if (!CallReserveSafely(
                m_reserve,
                acEffect.ReserveRequest,
                reservation,
                rawStatus))
        {
            return FailUnknown(acEffect);
        }

        outcome.RawStatus = rawStatus;
        if (rawStatus ==
            static_cast<uint32_t>(
                PartyQuestNativeLoadBridgeStatus::Reserved))
        {
            outcome.HasReservation = 1u;
            outcome.Reservation = reservation;
            if (!PartyQuestNativeLoadBridgePolicy::IsValidReservation(
                    reservation) ||
                !IdentityEquals(
                    reservation.Identity,
                    acEffect.ReserveRequest.Identity))
            {
                m_poisoned = 1u;
            }
        }
        break;
    }

    case PartyQuestNativeLoadBridgeOwnerEffectKind::Cancel:
        if (!CallNonceSafely(
                m_cancel,
                acEffect.AttemptNonce,
                rawStatus))
        {
            return FailUnknown(acEffect);
        }
        outcome.RawStatus = rawStatus;
        break;

    case PartyQuestNativeLoadBridgeOwnerEffectKind::Poll:
    {
        PartyQuestNativeLoadBridgeCompletionV1 completion{};
        if (!CallPollSafely(
                m_poll,
                acEffect.AttemptNonce,
                completion,
                rawStatus))
        {
            return FailUnknown(acEffect);
        }

        outcome.RawStatus = rawStatus;
        if (rawStatus ==
            static_cast<uint32_t>(
                PartyQuestNativeLoadBridgeStatus::
                    CompletionAvailable))
        {
            outcome.HasCompletion = 1u;
            outcome.Completion = completion;
            if (!PartyQuestNativeLoadBridgePolicy::IsValidCompletion(
                    completion) ||
                completion.AttemptNonce != acEffect.AttemptNonce)
            {
                m_poisoned = 1u;
            }
        }
        break;
    }

    case PartyQuestNativeLoadBridgeOwnerEffectKind::Retire:
        if (!CallNonceSafely(
                m_retire,
                acEffect.AttemptNonce,
                rawStatus))
        {
            return FailUnknown(acEffect);
        }
        outcome.RawStatus = rawStatus;
        break;

    case PartyQuestNativeLoadBridgeOwnerEffectKind::None:
        return FailUnknown(acEffect);
    }

    if (!IsAllowedStatus(acEffect.Kind, outcome.RawStatus))
        m_poisoned = 1u;

    return outcome;
}

bool PartyQuestSkyrimNativeLoadBridgeModuleLease::Matches(
    const PartyQuestNativeLoadBridgeCallCapability& acCapability,
    const PartyQuestNativeLoadBridgeOwnerEffect& acEffect) const noexcept
{
    if (!acCapability.IsAuthorized() ||
        acCapability.BoundGeneration != m_boundGeneration ||
        acCapability.RuntimeFingerprint != m_runtimeFingerprint ||
        acCapability.EffectKind != acEffect.Kind ||
        acCapability.EffectSequence == 0u ||
        acCapability.EffectSequence != acEffect.Sequence ||
        acEffect.Sequence == 0u ||
        !AreZero(acEffect.Reserved, sizeof(acEffect.Reserved)))
    {
        return false;
    }

    if (acEffect.Kind ==
        PartyQuestNativeLoadBridgeOwnerEffectKind::Reserve)
    {
        return acEffect.AttemptNonce == 0u &&
            PartyQuestNativeLoadBridgePolicy::IsValidReserveRequest(
                acEffect.ReserveRequest) &&
            acCapability.AuthorizesExactReserve(
                acEffect.ReserveRequest.Identity);
    }

    if (!IsZeroReserveRequest(acEffect.ReserveRequest))
        return false;

    return acCapability.AuthorizesExactCall(
        acEffect.Kind,
        acEffect.AttemptNonce);
}

PartyQuestNativeLoadBridgeOwnerForeignOutcome
PartyQuestSkyrimNativeLoadBridgeModuleLease::FailUnknown(
    const PartyQuestNativeLoadBridgeOwnerEffect& acEffect) noexcept
{
    m_poisoned = 1u;

    PartyQuestNativeLoadBridgeOwnerForeignOutcome outcome{};
    outcome.EffectKind = acEffect.Kind;
    outcome.Disposition =
        PartyQuestNativeLoadBridgeOwnerForeignDisposition::Unknown;
    outcome.EffectSequence = acEffect.Sequence;
    return outcome;
}

void PartyQuestSkyrimNativeLoadBridgeModuleLease::ResetMovedFrom() noexcept
{
    m_module = nullptr;
    m_reserve = nullptr;
    m_cancel = nullptr;
    m_poll = nullptr;
    m_retire = nullptr;
    m_boundGeneration = 0u;
    m_runtimeFingerprint = 0u;
    m_poisoned = 0u;
}
