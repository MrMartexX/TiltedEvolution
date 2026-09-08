#include <TiltedOnlinePCH.h>

#include <PartyQuestSkyrimPapyrusRuntimeObserver.h>

#include <Misc/BSScript.h>
#include <Misc/GameVM.h>
#include <Structs/Skyrim/PartyQuestSkyrimPapyrusRuntimeEvidence.h>
#include <Structs/Skyrim/PartyQuestSkyrimPapyrusRuntimeProfileResolver.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestPapyrusHashMapLayout.h>
#include <VersionDb.h>

#include <algorithm>
#include <array>
#include <limits>

namespace
{
constexpr PartyQuestSkyrimRuntimeVersion kRuntime161170{1, 6, 1170, 0};
constexpr PartyQuestSkyrimRuntimeVersion kRuntime17104{1, 7, 104, 0};
constexpr uint32_t kInternalVirtualMachineVtableId = 252631;
constexpr size_t kInternalVirtualMachineReadableSize = 0x9380;
constexpr uint32_t kMaximumPlausibleDomainCount = 1u << 24;
constexpr uint32_t kMaximumLinkedFunctionMessages = 1u << 20;

constexpr size_t kFunctionQueueLockOffset = 0x0200;
constexpr size_t kFunctionMessageQueueOffset = 0x8220;
constexpr size_t kOverflowFunctionMessagesOffset = 0x8248;
constexpr size_t kVmTasksOffset = 0x8260;
constexpr size_t kUiWaitingFunctionMessagesOffset = 0x8278;
constexpr size_t kSuspendQueue1Offset = 0x8280;
constexpr size_t kSuspendQueue2Offset = 0x8AA0;
constexpr size_t kOverflowSuspendArray1Offset = 0x92C0;
constexpr size_t kOverflowSuspendArray2Offset = 0x92D8;
constexpr size_t kSuspendQueueLockOffset = 0x92F0;
constexpr size_t kRunningStacksLockOffset = 0x9318;
constexpr size_t kAllRunningStacksOffset = 0x9320;
constexpr size_t kWaitingLatentReturnsOffset = 0x9350;

constexpr size_t kCommonQueueLockOffset = 0x08;
constexpr size_t kLinkedQueueHeadOffset = 0x18;
constexpr size_t kFunctionMessageSize = 0x18;
constexpr size_t kStaticQueueNumEntriesOffset = 0x810;
constexpr size_t kHashMapEntrySize = 0x18;

struct RawSpinLock final
{
    volatile LONG OwningThread;
    volatile LONG LockCount;
};
static_assert(sizeof(RawSpinLock) == 0x8);

struct RawArray final
{
    void* Data;
    uint32_t Capacity;
    uint32_t Padding0C;
    uint32_t Size;
    uint32_t Padding14;
};
static_assert(sizeof(RawArray) == 0x18);

struct RawHashMap final
{
    uint8_t Padding00[0x0C];
    uint32_t Capacity;
    uint32_t Free;
    uint32_t Good;
    const void* Sentinel;
    uint64_t AllocatorPadding20;
    void* Entries;
};
static_assert(sizeof(RawHashMap) == 0x30);
static_assert(offsetof(RawHashMap, Entries) == 0x28);

bool IsReadableProtection(DWORD aProtection) noexcept
{
    if ((aProtection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;

    const DWORD base = aProtection & 0xFF;
    return base == PAGE_READONLY ||
        base == PAGE_READWRITE ||
        base == PAGE_WRITECOPY ||
        base == PAGE_EXECUTE_READ ||
        base == PAGE_EXECUTE_READWRITE ||
        base == PAGE_EXECUTE_WRITECOPY;
}

bool IsWritableProtection(DWORD aProtection) noexcept
{
    if ((aProtection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;

    const DWORD base = aProtection & 0xFF;
    return base == PAGE_READWRITE ||
        base == PAGE_WRITECOPY ||
        base == PAGE_EXECUTE_READWRITE ||
        base == PAGE_EXECUTE_WRITECOPY;
}

bool IsRangeWithProtection(
    const void* apData,
    size_t aSize,
    bool aRequireWritable) noexcept
{
    if (!apData || aSize == 0)
        return false;

    const uintptr_t start = reinterpret_cast<uintptr_t>(apData);
    if (start > std::numeric_limits<uintptr_t>::max() - aSize)
        return false;
    const uintptr_t end = start + aSize;

    uintptr_t cursor = start;
    while (cursor < end)
    {
        MEMORY_BASIC_INFORMATION information{};
        if (::VirtualQuery(
                reinterpret_cast<const void*>(cursor),
                &information,
                sizeof(information)) != sizeof(information) ||
            information.State != MEM_COMMIT ||
            !(aRequireWritable
                ? IsWritableProtection(information.Protect)
                : IsReadableProtection(information.Protect)))
        {
            return false;
        }

        const uintptr_t region =
            reinterpret_cast<uintptr_t>(information.BaseAddress);
        if (region > std::numeric_limits<uintptr_t>::max() -
                information.RegionSize)
        {
            return false;
        }
        const uintptr_t next = region + information.RegionSize;
        if (next <= cursor)
            return false;
        cursor = next;
    }

    return true;
}

bool IsReadableRange(const void* apData, size_t aSize) noexcept
{
    return IsRangeWithProtection(apData, aSize, false);
}

bool IsWritableRange(const void* apData, size_t aSize) noexcept
{
    return IsRangeWithProtection(apData, aSize, true);
}

bool IsExecutableAddress(const void* apAddress) noexcept
{
    if (!apAddress)
        return false;

    MEMORY_BASIC_INFORMATION information{};
    if (::VirtualQuery(apAddress, &information, sizeof(information)) !=
            sizeof(information) ||
        information.State != MEM_COMMIT ||
        (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
    {
        return false;
    }

    const DWORD base = information.Protect & 0xFF;
    return base == PAGE_EXECUTE ||
        base == PAGE_EXECUTE_READ ||
        base == PAGE_EXECUTE_READWRITE ||
        base == PAGE_EXECUTE_WRITECOPY;
}

class ScopedRawSpinLock final
{
public:
    ScopedRawSpinLock() noexcept = default;
    ScopedRawSpinLock(const ScopedRawSpinLock&) = delete;
    ScopedRawSpinLock& operator=(const ScopedRawSpinLock&) = delete;

    ~ScopedRawSpinLock() noexcept
    {
        Release();
    }

    [[nodiscard]] bool TryAcquire(RawSpinLock* apLock) noexcept
    {
        if (m_pLock || !apLock ||
            !IsWritableRange(apLock, sizeof(*apLock)))
        {
            return false;
        }

        const LONG threadId = static_cast<LONG>(::GetCurrentThreadId());
        if (apLock->OwningThread == threadId)
        {
            if (::InterlockedIncrement(&apLock->LockCount) <= 1)
            {
                (void)::InterlockedDecrement(&apLock->LockCount);
                return false;
            }
            m_pLock = apLock;
            return true;
        }

        if (::InterlockedCompareExchange(&apLock->LockCount, 1, 0) != 0)
            return false;

        ::InterlockedExchange(&apLock->OwningThread, threadId);
        m_pLock = apLock;
        return true;
    }

private:
    void Release() noexcept
    {
        if (!m_pLock)
            return;

        const LONG threadId = static_cast<LONG>(::GetCurrentThreadId());
        if (m_pLock->OwningThread == threadId)
        {
            if (m_pLock->LockCount == 1)
            {
                ::InterlockedExchange(&m_pLock->OwningThread, 0);
                (void)::InterlockedCompareExchange(&m_pLock->LockCount, 0, 1);
            }
            else
            {
                (void)::InterlockedDecrement(&m_pLock->LockCount);
            }
        }
        m_pLock = nullptr;
    }

    RawSpinLock* m_pLock{};
};

class ScopedCommonQueueLock final
{
public:
    ScopedCommonQueueLock() noexcept = default;
    ScopedCommonQueueLock(const ScopedCommonQueueLock&) = delete;
    ScopedCommonQueueLock& operator=(const ScopedCommonQueueLock&) = delete;

    ~ScopedCommonQueueLock() noexcept
    {
        if (m_pLock)
            ::InterlockedExchange(m_pLock, 0);
    }

    [[nodiscard]] bool TryAcquire(volatile LONG* apLock) noexcept
    {
        if (m_pLock || !apLock ||
            !IsWritableRange(
                const_cast<const LONG*>(apLock),
                sizeof(*apLock)) ||
            ::InterlockedCompareExchange(apLock, 1, 0) != 0)
        {
            return false;
        }

        m_pLock = apLock;
        return true;
    }

private:
    volatile LONG* m_pLock{};
};

template <class T>
T* At(void* apBase, size_t aOffset) noexcept
{
    return reinterpret_cast<T*>(
        reinterpret_cast<uint8_t*>(apBase) + aOffset);
}

bool TryArrayCount(
    void* apVm,
    size_t aOffset,
    size_t aElementSize,
    uint32_t& aOut) noexcept
{
    const auto* pArray = At<const RawArray>(apVm, aOffset);
    if (!IsReadableRange(pArray, sizeof(*pArray)) ||
        pArray->Size > pArray->Capacity ||
        pArray->Size > kMaximumPlausibleDomainCount)
    {
        return false;
    }

    if (pArray->Capacity == 0)
    {
        if (pArray->Size != 0)
            return false;
    }
    else
    {
        if (!pArray->Data || aElementSize == 0 ||
            pArray->Capacity >
                std::numeric_limits<size_t>::max() / aElementSize)
        {
            return false;
        }

        const size_t bytes =
            static_cast<size_t>(pArray->Capacity) * aElementSize;
        const auto* pLast = reinterpret_cast<const uint8_t*>(pArray->Data) +
            bytes - 1;
        if (!IsReadableRange(pArray->Data, 1) || !IsReadableRange(pLast, 1))
            return false;
    }

    aOut = pArray->Size;
    return true;
}

bool TryHashCount(
    void* apVm,
    size_t aOffset,
    uint32_t& aOut,
    PartyQuestSkyrimPapyrusHashMapDiagnostic* apDiagnostic = nullptr) noexcept
{
    const auto* pMap = At<const RawHashMap>(apVm, aOffset);
    if (!IsReadableRange(pMap, sizeof(*pMap)))
        return false;

    if (apDiagnostic)
    {
        apDiagnostic->Capacity = pMap->Capacity;
        apDiagnostic->Free = pMap->Free;
        apDiagnostic->FreeSearchStart = pMap->Good;
        apDiagnostic->EntriesPresent = pMap->Entries != nullptr;
    }

    if (!IsPlausiblePartyQuestPapyrusHashMapHeader(
            pMap->Capacity,
            pMap->Free,
            pMap->Good))
    {
        return false;
    }

    if (pMap->Capacity == 0)
    {
        if (pMap->Free != 0 || pMap->Entries != nullptr)
            return false;
    }
    else
    {
        if (!pMap->Entries ||
            pMap->Capacity >
                std::numeric_limits<size_t>::max() / kHashMapEntrySize)
        {
            return false;
        }
        const size_t bytes =
            static_cast<size_t>(pMap->Capacity) * kHashMapEntrySize;
        const auto* pLast = reinterpret_cast<const uint8_t*>(pMap->Entries) +
            bytes - 1;
        const bool entriesRangeReadable =
            IsReadableRange(pMap->Entries, 1) && IsReadableRange(pLast, 1);
        if (apDiagnostic)
            apDiagnostic->EntriesRangeReadable = entriesRangeReadable;
        if (!entriesRangeReadable)
            return false;
    }

    aOut = pMap->Capacity - pMap->Free;
    return true;
}

bool TryLinkedFunctionMessageCount(void* apVm, uint32_t& aOut) noexcept
{
    auto* pNode = *At<void*>(
        apVm,
        kFunctionMessageQueueOffset + kLinkedQueueHeadOffset);
    uint32_t count = 0;
    while (pNode)
    {
        if (count == kMaximumLinkedFunctionMessages ||
            !IsReadableRange(pNode, kFunctionMessageSize + sizeof(void*)))
        {
            return false;
        }
        pNode = *At<void*>(pNode, kFunctionMessageSize);
        ++count;
    }

    aOut = count;
    return true;
}

bool TryStaticQueueCount(void* apVm, size_t aOffset, uint32_t& aOut) noexcept
{
    const auto* pCount = At<const uint32_t>(
        apVm,
        aOffset + kStaticQueueNumEntriesOffset);
    if (!IsReadableRange(pCount, sizeof(*pCount)) || *pCount > 128)
        return false;
    aOut = *pCount;
    return true;
}

bool TryAdd(uint32_t aLeft, uint32_t aRight, uint32_t& aOut) noexcept
{
    if (aLeft > std::numeric_limits<uint32_t>::max() - aRight)
        return false;
    aOut = aLeft + aRight;
    return aOut <= kMaximumPlausibleDomainCount;
}

VersionDbPtr<void*> s_internalVirtualMachineVtable(
    kInternalVirtualMachineVtableId);
bool s_ingressHooksRegistered = false;

using TSendEvent = void(
    void*, uint64_t, const void*, BSScript::IFunctionArguments*);
using TSendEventAll = void(
    void*, const void*, BSScript::IFunctionArguments*);
using TReturnFromLatent = void(void*, uint32_t, const void*);

TSendEvent* s_sendEvent{};
TSendEventAll* s_sendEventAll{};
TReturnFromLatent* s_returnFromLatent{};

bool IsSupportedRuntimeIdentity(
    const PartyQuestSkyrimRuntimeIdentityAuthorization& acIdentity) noexcept
{
    if (!acIdentity.IsVerified())
        return false;

    if (acIdentity.GetRuntimeVersion().Matches(kRuntime161170))
    {
        uint64_t vtableOffset = 0;
        const bool found = VersionDb::Get().FindOffsetById(
            kInternalVirtualMachineVtableId, vtableOffset);
        const PartyQuestSkyrimPapyrusRuntimeEvidence evidence{
            acIdentity.GetRuntimeVersion(),
            acIdentity.GetExecutableIdentity(),
            VersionDb::Get().IsLoaded(),
            VersionDb::Get().GetLoadedDatabaseFormat(),
            kInternalVirtualMachineVtableId,
            found ? vtableOffset : 0,
            true,
            true};
        return evidence.Validate() ==
            PartyQuestSkyrimPapyrusRuntimeEvidenceStatus::Supported;
    }

    // Retain the existing 1.7.104 diagnostic surface. It still cannot issue a
    // production profile while its stable executable identity is unregistered.
    return acIdentity.GetRuntimeVersion().Matches(kRuntime17104);
}
} // namespace

class PartyQuestSkyrimPapyrusGenerationSourceResolver final
{
public:
    [[nodiscard]] static PartyQuestPapyrusRuntimeGenerationAuthorization
    Resolve(
        const PartyQuestSkyrimRuntimeIdentityAuthorization& acIdentity) noexcept
    {
        if (!s_ingressHooksRegistered ||
            !acIdentity.GetRuntimeVersion().Matches(kRuntime161170) ||
            !IsSupportedRuntimeIdentity(acIdentity))
        {
            return {};
        }

        return PartyQuestPapyrusRuntimeGenerationAuthorization(
            kRuntime161170,
            0x505147454E313631ull,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains,
            true,
            true,
            true);
    }
};

class PartyQuestSkyrimPapyrusSnapshotResolver final
{
public:
    [[nodiscard]] static PartyQuestPapyrusRuntimeSnapshotAuthorization Resolve(
        const PartyQuestSkyrimRuntimeIdentityAuthorization& acIdentity) noexcept
    {
        if (!s_ingressHooksRegistered ||
            !acIdentity.GetRuntimeVersion().Matches(kRuntime161170) ||
            !IsSupportedRuntimeIdentity(acIdentity))
        {
            return {};
        }

        return PartyQuestPapyrusRuntimeSnapshotAuthorization(
            kRuntime161170,
            0x5051534E50313631ull,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains,
            true,
            true,
            true);
    }
};

class PartyQuestSkyrimPapyrusHookBridge final
{
public:
    static PartyQuestPapyrusIngressEpoch::Scope Begin() noexcept
    {
        return PartyQuestSkyrimPapyrusRuntimeObserver::GetProcessObserver().
            BeginIngress();
    }

};

namespace
{
void HookSendEvent(
    void* apVm,
    uint64_t aHandle,
    const void* apEventName,
    BSScript::IFunctionArguments* apArguments)
{
    auto ingress = PartyQuestSkyrimPapyrusHookBridge::Begin();
    s_sendEvent(apVm, aHandle, apEventName, apArguments);
}

void HookSendEventAll(
    void* apVm,
    const void* apEventName,
    BSScript::IFunctionArguments* apArguments)
{
    auto ingress = PartyQuestSkyrimPapyrusHookBridge::Begin();
    s_sendEventAll(apVm, apEventName, apArguments);
}

void HookReturnFromLatent(
    void* apVm,
    uint32_t aStackId,
    const void* apValue)
{
    auto ingress = PartyQuestSkyrimPapyrusHookBridge::Begin();
    s_returnFromLatent(apVm, aStackId, apValue);
}

bool ResolveIngressHookTargets() noexcept
{
    const auto identity = PartyQuestSkyrimRuntimeIdentityResolver::Resolve();
    if (!IsSupportedRuntimeIdentity(identity))
    {
        return false;
    }

    auto** pVtable = reinterpret_cast<void**>(
        s_internalVirtualMachineVtable.GetPtr());
    if (!IsReadableRange(pVtable, sizeof(void*) * 0x2C))
        return false;

    constexpr std::array<size_t, 3> indices{0x24, 0x25, 0x2B};
    for (const size_t index : indices)
    {
        if (!IsExecutableAddress(pVtable[index]))
            return false;
    }

    s_sendEvent = reinterpret_cast<TSendEvent*>(pVtable[0x24]);
    s_sendEventAll = reinterpret_cast<TSendEventAll*>(pVtable[0x25]);
    s_returnFromLatent =
        reinterpret_cast<TReturnFromLatent*>(pVtable[0x2B]);
    return true;
}

static TiltedPhoques::Initializer s_partyQuestPapyrusObserverHooks(
    []()
    {
        if (!ResolveIngressHookTargets())
            return;

        TP_HOOK(&s_sendEvent, HookSendEvent);
        TP_HOOK(&s_sendEventAll, HookSendEventAll);
        TP_HOOK(&s_returnFromLatent, HookReturnFromLatent);
        // The core Update/UpdateTasklets/TasksToJobs detours are deliberately
        // disabled after live evidence showed that installing the Task 03 hook
        // set prevented ordinary follower, guard and courier Papyrus behavior.
        // Event ingress alone is insufficient to authorize a coherent runtime
        // snapshot, so keep publication fail-closed until an execution fence
        // with proven non-interference is available.
        s_ingressHooksRegistered = false;
    });
} // namespace

PartyQuestSkyrimPapyrusRuntimeObserver&
PartyQuestSkyrimPapyrusRuntimeObserver::GetProcessObserver() noexcept
{
    static PartyQuestSkyrimPapyrusRuntimeObserver s_observer;
    return s_observer;
}

PartyQuestPapyrusRuntimeObservation
PartyQuestSkyrimPapyrusRuntimeObserver::Observe(uint64_t) noexcept
{
    return SampleDiagnostics().Observation;
}

PartyQuestSkyrimPapyrusDiagnosticSample
PartyQuestSkyrimPapyrusRuntimeObserver::SampleDiagnostics() noexcept
{
    PartyQuestSkyrimPapyrusDiagnosticSample result;
    auto& processFence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const uint64_t processGenerationBefore = processFence.GetGeneration();
    result.ProcessGeneration = processGenerationBefore;
    if (processFence.IsLifecycleTransitionPending())
    {
        result.DiagnosticStatus =
            PartyQuestSkyrimPapyrusDiagnosticStatus::GenerationChanged;
        return result;
    }
    result.IngressHookInvocationCount = GetIngressHookInvocationCount();
    result.IngressHooksRegistered = s_ingressHooksRegistered;

    const auto identity = PartyQuestSkyrimRuntimeIdentityResolver::Resolve();
    result.ExactRuntimeIdentity = IsSupportedRuntimeIdentity(identity);
    if (!result.ExactRuntimeIdentity)
    {
        result.DiagnosticStatus =
            PartyQuestSkyrimPapyrusDiagnosticStatus::UnsupportedRuntime;
        result.Observation.Status =
            PartyQuestPapyrusRuntimeObservationStatus::Unsupported;
        return result;
    }

    const auto before = m_ingressEpoch.Capture();
    result.Observation.QuestEventGeneration = before.Generation;
    if (!s_ingressHooksRegistered || !before.Healthy)
    {
        result.DiagnosticStatus =
            PartyQuestSkyrimPapyrusDiagnosticStatus::HooksUnavailable;
        return result;
    }

    SkyrimVM* pGameVm = GameVM::Get();
    void* pVm = pGameVm ? pGameVm->virtualMachine : nullptr;
    if (!pVm)
    {
        result.DiagnosticStatus =
            PartyQuestSkyrimPapyrusDiagnosticStatus::VirtualMachineUnavailable;
        return result;
    }
    if (!IsReadableRange(pVm, kInternalVirtualMachineReadableSize))
    {
        result.DiagnosticStatus =
            PartyQuestSkyrimPapyrusDiagnosticStatus::MemoryValidationFailed;
        return result;
    }

    auto** pObservedVtable = *reinterpret_cast<void***>(pVm);
    auto** pExpectedVtable = reinterpret_cast<void**>(
        s_internalVirtualMachineVtable.GetPtr());
    result.VirtualTableMatched = pObservedVtable == pExpectedVtable;
    if (!result.VirtualTableMatched)
    {
        result.DiagnosticStatus =
            PartyQuestSkyrimPapyrusDiagnosticStatus::VirtualTableMismatch;
        return result;
    }

    ScopedRawSpinLock functionLock;
    ScopedCommonQueueLock functionQueueLock;
    ScopedRawSpinLock suspendLock;
    ScopedCommonQueueLock suspendQueue1Lock;
    ScopedCommonQueueLock suspendQueue2Lock;
    ScopedRawSpinLock runningStacksLock;
    if (!functionLock.TryAcquire(At<RawSpinLock>(pVm, kFunctionQueueLockOffset)) ||
        !functionQueueLock.TryAcquire(At<volatile LONG>(
            pVm,
            kFunctionMessageQueueOffset + kCommonQueueLockOffset)) ||
        !suspendLock.TryAcquire(At<RawSpinLock>(pVm, kSuspendQueueLockOffset)) ||
        !suspendQueue1Lock.TryAcquire(At<volatile LONG>(
            pVm,
            kSuspendQueue1Offset + kCommonQueueLockOffset)) ||
        !suspendQueue2Lock.TryAcquire(At<volatile LONG>(
            pVm,
            kSuspendQueue2Offset + kCommonQueueLockOffset)) ||
        !runningStacksLock.TryAcquire(At<RawSpinLock>(
            pVm,
            kRunningStacksLockOffset)))
    {
        result.DiagnosticStatus =
            PartyQuestSkyrimPapyrusDiagnosticStatus::LockContended;
        return result;
    }

    uint32_t linkedFunctions = 0;
    uint32_t overflowFunctions = 0;
    uint32_t vmTasks = 0;
    uint32_t suspend1 = 0;
    uint32_t suspend2 = 0;
    uint32_t overflowSuspend1 = 0;
    uint32_t overflowSuspend2 = 0;
    const auto rejectLayout = [&result](
                                  PartyQuestSkyrimPapyrusLayoutFailure aFailure)
    {
        result.LayoutFailure = aFailure;
        result.DiagnosticStatus =
            PartyQuestSkyrimPapyrusDiagnosticStatus::LayoutValidationFailed;
    };
    if (!TryLinkedFunctionMessageCount(pVm, linkedFunctions))
        rejectLayout(PartyQuestSkyrimPapyrusLayoutFailure::LinkedFunctionMessages);
    else if (!TryArrayCount(
                 pVm,
                 kOverflowFunctionMessagesOffset,
                 kFunctionMessageSize,
                 overflowFunctions))
        rejectLayout(PartyQuestSkyrimPapyrusLayoutFailure::OverflowFunctionMessages);
    else if (!TryArrayCount(pVm, kVmTasksOffset, sizeof(void*), vmTasks))
        rejectLayout(PartyQuestSkyrimPapyrusLayoutFailure::VmTasks);
    else if (!TryStaticQueueCount(pVm, kSuspendQueue1Offset, suspend1))
        rejectLayout(PartyQuestSkyrimPapyrusLayoutFailure::SuspendQueue1);
    else if (!TryStaticQueueCount(pVm, kSuspendQueue2Offset, suspend2))
        rejectLayout(PartyQuestSkyrimPapyrusLayoutFailure::SuspendQueue2);
    else if (!TryArrayCount(
                 pVm,
                 kOverflowSuspendArray1Offset,
                 0x10,
                 overflowSuspend1))
        rejectLayout(PartyQuestSkyrimPapyrusLayoutFailure::OverflowSuspendArray1);
    else if (!TryArrayCount(
                 pVm,
                 kOverflowSuspendArray2Offset,
                 0x10,
                 overflowSuspend2))
        rejectLayout(PartyQuestSkyrimPapyrusLayoutFailure::OverflowSuspendArray2);
    else if (!TryHashCount(
                 pVm,
                 kAllRunningStacksOffset,
                 result.Counts.RunningStacks,
                 &result.FailedHashMap))
        rejectLayout(PartyQuestSkyrimPapyrusLayoutFailure::RunningStacks);
    else if (!TryHashCount(
                 pVm,
                 kWaitingLatentReturnsOffset,
                 result.Counts.LatentReturnQueue,
                 &result.FailedHashMap))
        rejectLayout(PartyQuestSkyrimPapyrusLayoutFailure::WaitingLatentReturns);
    else if (!TryAdd(
                 linkedFunctions,
                 overflowFunctions,
                 result.Counts.FunctionMessageQueues))
        rejectLayout(PartyQuestSkyrimPapyrusLayoutFailure::FunctionMessageTotal);
    else if (!TryAdd(suspend1, suspend2, result.Counts.SuspendResumeQueues) ||
             !TryAdd(
                 result.Counts.SuspendResumeQueues,
                 overflowSuspend1,
                 result.Counts.SuspendResumeQueues) ||
             !TryAdd(
                 result.Counts.SuspendResumeQueues,
                 overflowSuspend2,
                 result.Counts.SuspendResumeQueues))
        rejectLayout(PartyQuestSkyrimPapyrusLayoutFailure::SuspendResumeTotal);

    if (result.LayoutFailure != PartyQuestSkyrimPapyrusLayoutFailure::None)
    {
        return result;
    }

    result.Counts.VmTaskQueue = vmTasks;
    result.Counts.UiWaitingQueue = *At<const uint32_t>(
        pVm,
        kUiWaitingFunctionMessagesOffset);
    if (result.Counts.UiWaitingQueue > kMaximumPlausibleDomainCount)
    {
        result.LayoutFailure = PartyQuestSkyrimPapyrusLayoutFailure::UiWaiting;
        result.DiagnosticStatus =
            PartyQuestSkyrimPapyrusDiagnosticStatus::LayoutValidationFailed;
        return result;
    }

    const auto after = m_ingressEpoch.Capture();
    const uint64_t processGenerationAfter = processFence.GetGeneration();
    result.ProcessGeneration = processGenerationAfter;
    result.Observation.QuestEventGeneration = after.Generation;
    result.IngressHookInvocationCount = GetIngressHookInvocationCount();
    if (!PartyQuestPapyrusIngressEpoch::IsStable(before, after) ||
        processGenerationBefore != processGenerationAfter ||
        processFence.IsLifecycleTransitionPending())
    {
        result.DiagnosticStatus =
            PartyQuestSkyrimPapyrusDiagnosticStatus::GenerationChanged;
        return result;
    }

    uint32_t pending = 0;
    const std::array<uint32_t, 6> counts{
        result.Counts.FunctionMessageQueues,
        result.Counts.VmTaskQueue,
        result.Counts.UiWaitingQueue,
        result.Counts.SuspendResumeQueues,
        result.Counts.RunningStacks,
        result.Counts.LatentReturnQueue};
    for (const uint32_t count : counts)
    {
        if (!TryAdd(pending, count, pending))
        {
            result.LayoutFailure =
                PartyQuestSkyrimPapyrusLayoutFailure::PendingWorkTotal;
            result.DiagnosticStatus =
                PartyQuestSkyrimPapyrusDiagnosticStatus::LayoutValidationFailed;
            return result;
        }
    }

    result.Observation.PendingWorkCount = pending;
    result.Observation.ObservedWorkDomains =
        kPartyQuestPapyrusRuntimeRequiredWorkDomains;
    result.Observation.Status = pending == 0
        ? PartyQuestPapyrusRuntimeObservationStatus::Idle
        : PartyQuestPapyrusRuntimeObservationStatus::Busy;
    result.DiagnosticStatus =
        PartyQuestSkyrimPapyrusDiagnosticStatus::Sampled;
    return result;
}

PartyQuestPapyrusRuntimeObserverAuthorization
PartyQuestSkyrimPapyrusRuntimeObserver::Authorize() noexcept
{
    const auto identity = PartyQuestSkyrimRuntimeIdentityResolver::Resolve();
    const auto generation =
        PartyQuestSkyrimPapyrusGenerationSourceResolver::Resolve(identity);
    const auto snapshot =
        PartyQuestSkyrimPapyrusSnapshotResolver::Resolve(identity);
    const auto profile =
        PartyQuestSkyrimPapyrusRuntimeProfileResolver::Resolve(
            identity, generation, snapshot);
    return PartyQuestPapyrusRuntimeObserverAuthorization(*this, profile);
}

const char* PartyQuestSkyrimPapyrusRuntimeObserver::DiagnosticStatusName(
    PartyQuestSkyrimPapyrusDiagnosticStatus aStatus) noexcept
{
    switch (aStatus)
    {
    case PartyQuestSkyrimPapyrusDiagnosticStatus::Sampled:
        return "sampled";
    case PartyQuestSkyrimPapyrusDiagnosticStatus::UnsupportedRuntime:
        return "unsupported-runtime";
    case PartyQuestSkyrimPapyrusDiagnosticStatus::HooksUnavailable:
        return "ingress-hooks-unavailable";
    case PartyQuestSkyrimPapyrusDiagnosticStatus::VirtualMachineUnavailable:
        return "virtual-machine-unavailable";
    case PartyQuestSkyrimPapyrusDiagnosticStatus::VirtualTableMismatch:
        return "virtual-table-mismatch";
    case PartyQuestSkyrimPapyrusDiagnosticStatus::MemoryValidationFailed:
        return "memory-validation-failed";
    case PartyQuestSkyrimPapyrusDiagnosticStatus::LockContended:
        return "lock-contended";
    case PartyQuestSkyrimPapyrusDiagnosticStatus::LayoutValidationFailed:
        return "layout-validation-failed";
    case PartyQuestSkyrimPapyrusDiagnosticStatus::GenerationChanged:
        return "generation-changed-during-snapshot";
    }
    return "unknown";
}

const char* PartyQuestSkyrimPapyrusRuntimeObserver::LayoutFailureName(
    PartyQuestSkyrimPapyrusLayoutFailure aFailure) noexcept
{
    switch (aFailure)
    {
    case PartyQuestSkyrimPapyrusLayoutFailure::None:
        return "none";
    case PartyQuestSkyrimPapyrusLayoutFailure::LinkedFunctionMessages:
        return "linked-function-messages";
    case PartyQuestSkyrimPapyrusLayoutFailure::OverflowFunctionMessages:
        return "overflow-function-messages";
    case PartyQuestSkyrimPapyrusLayoutFailure::VmTasks:
        return "vm-tasks";
    case PartyQuestSkyrimPapyrusLayoutFailure::SuspendQueue1:
        return "suspend-queue-1";
    case PartyQuestSkyrimPapyrusLayoutFailure::SuspendQueue2:
        return "suspend-queue-2";
    case PartyQuestSkyrimPapyrusLayoutFailure::OverflowSuspendArray1:
        return "overflow-suspend-array-1";
    case PartyQuestSkyrimPapyrusLayoutFailure::OverflowSuspendArray2:
        return "overflow-suspend-array-2";
    case PartyQuestSkyrimPapyrusLayoutFailure::RunningStacks:
        return "running-stacks";
    case PartyQuestSkyrimPapyrusLayoutFailure::WaitingLatentReturns:
        return "waiting-latent-returns";
    case PartyQuestSkyrimPapyrusLayoutFailure::FunctionMessageTotal:
        return "function-message-total";
    case PartyQuestSkyrimPapyrusLayoutFailure::SuspendResumeTotal:
        return "suspend-resume-total";
    case PartyQuestSkyrimPapyrusLayoutFailure::UiWaiting:
        return "ui-waiting";
    case PartyQuestSkyrimPapyrusLayoutFailure::PendingWorkTotal:
        return "pending-work-total";
    }
    return "unknown";
}
