#include <TiltedOnlinePCH.h>

#include <PartyQuestSkyrimPapyrusRuntimeObserver.h>

#include <Misc/BSScript.h>
#include <Misc/GameVM.h>
#include <Structs/Skyrim/PartyQuestSkyrimPapyrusRuntimeProfileResolver.h>
#include <VersionDb.h>

#include <algorithm>
#include <array>
#include <bit>
#include <limits>

namespace
{
constexpr PartyQuestSkyrimRuntimeVersion kSupportedRuntime{1, 7, 104, 0};
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
    void* Entries;
    uint8_t Padding28[0x08];
};
static_assert(sizeof(RawHashMap) == 0x30);

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

bool TryHashCount(void* apVm, size_t aOffset, uint32_t& aOut) noexcept
{
    const auto* pMap = At<const RawHashMap>(apVm, aOffset);
    if (!IsReadableRange(pMap, sizeof(*pMap)) ||
        pMap->Free > pMap->Capacity ||
        pMap->Capacity > kMaximumPlausibleDomainCount ||
        (pMap->Capacity != 0 && !std::has_single_bit(pMap->Capacity)) ||
        (pMap->Capacity != 0 && pMap->Good >= pMap->Capacity))
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
        if (!IsReadableRange(pMap->Entries, 1) || !IsReadableRange(pLast, 1))
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
using TDispatchStaticCall = bool(
    void*, const void*, const void*, BSScript::IFunctionArguments*, void*);
using TDispatchMethodCall1 = bool(
    void*, void*, const void*, BSScript::IFunctionArguments*, void*);
using TDispatchMethodCall2 = bool(
    void*, uint64_t, const void*, const void*, BSScript::IFunctionArguments*, void*);
using TDispatchUnboundMethodCall = bool(void*);
using TReturnFromLatent = void(void*, uint32_t, const void*);

TSendEvent* s_sendEvent{};
TSendEventAll* s_sendEventAll{};
TDispatchStaticCall* s_dispatchStaticCall{};
TDispatchMethodCall1* s_dispatchMethodCall1{};
TDispatchMethodCall2* s_dispatchMethodCall2{};
TDispatchUnboundMethodCall* s_dispatchUnboundMethodCall{};
TReturnFromLatent* s_returnFromLatent{};
} // namespace

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

bool HookDispatchStaticCall(
    void* apVm,
    const void* apClassName,
    const void* apFunctionName,
    BSScript::IFunctionArguments* apArguments,
    void* apResult)
{
    auto ingress = PartyQuestSkyrimPapyrusHookBridge::Begin();
    return s_dispatchStaticCall(
        apVm, apClassName, apFunctionName, apArguments, apResult);
}

bool HookDispatchMethodCall1(
    void* apVm,
    void* apObject,
    const void* apFunctionName,
    BSScript::IFunctionArguments* apArguments,
    void* apResult)
{
    auto ingress = PartyQuestSkyrimPapyrusHookBridge::Begin();
    return s_dispatchMethodCall1(
        apVm, apObject, apFunctionName, apArguments, apResult);
}

bool HookDispatchMethodCall2(
    void* apVm,
    uint64_t aHandle,
    const void* apClassName,
    const void* apFunctionName,
    BSScript::IFunctionArguments* apArguments,
    void* apResult)
{
    auto ingress = PartyQuestSkyrimPapyrusHookBridge::Begin();
    return s_dispatchMethodCall2(
        apVm, aHandle, apClassName, apFunctionName, apArguments, apResult);
}

bool HookDispatchUnboundMethodCall(void* apVm)
{
    auto ingress = PartyQuestSkyrimPapyrusHookBridge::Begin();
    return s_dispatchUnboundMethodCall(apVm);
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
    if (!identity.IsVerified() ||
        !identity.GetRuntimeVersion().Matches(kSupportedRuntime))
    {
        return false;
    }

    auto** pVtable = reinterpret_cast<void**>(
        s_internalVirtualMachineVtable.GetPtr());
    if (!IsReadableRange(pVtable, sizeof(void*) * 0x2C))
        return false;

    constexpr std::array<size_t, 7> indices{
        0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2B};
    for (const size_t index : indices)
    {
        if (!IsExecutableAddress(pVtable[index]))
            return false;
    }

    s_sendEvent = reinterpret_cast<TSendEvent*>(pVtable[0x24]);
    s_sendEventAll = reinterpret_cast<TSendEventAll*>(pVtable[0x25]);
    s_dispatchStaticCall =
        reinterpret_cast<TDispatchStaticCall*>(pVtable[0x26]);
    s_dispatchMethodCall1 =
        reinterpret_cast<TDispatchMethodCall1*>(pVtable[0x27]);
    s_dispatchMethodCall2 =
        reinterpret_cast<TDispatchMethodCall2*>(pVtable[0x28]);
    s_dispatchUnboundMethodCall =
        reinterpret_cast<TDispatchUnboundMethodCall*>(pVtable[0x29]);
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
        TP_HOOK(&s_dispatchStaticCall, HookDispatchStaticCall);
        TP_HOOK(&s_dispatchMethodCall1, HookDispatchMethodCall1);
        TP_HOOK(&s_dispatchMethodCall2, HookDispatchMethodCall2);
        TP_HOOK(&s_dispatchUnboundMethodCall, HookDispatchUnboundMethodCall);
        TP_HOOK(&s_returnFromLatent, HookReturnFromLatent);
        s_ingressHooksRegistered = true;
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
    result.IngressHookInvocationCount = GetIngressHookInvocationCount();
    result.IngressHooksRegistered = s_ingressHooksRegistered;

    const auto identity = PartyQuestSkyrimRuntimeIdentityResolver::Resolve();
    result.ExactRuntimeIdentity = identity.IsVerified() &&
        identity.GetRuntimeVersion().Matches(kSupportedRuntime);
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
    if (!TryLinkedFunctionMessageCount(pVm, linkedFunctions) ||
        !TryArrayCount(
            pVm,
            kOverflowFunctionMessagesOffset,
            kFunctionMessageSize,
            overflowFunctions) ||
        !TryArrayCount(pVm, kVmTasksOffset, sizeof(void*), vmTasks) ||
        !TryStaticQueueCount(pVm, kSuspendQueue1Offset, suspend1) ||
        !TryStaticQueueCount(pVm, kSuspendQueue2Offset, suspend2) ||
        !TryArrayCount(
            pVm,
            kOverflowSuspendArray1Offset,
            0x10,
            overflowSuspend1) ||
        !TryArrayCount(
            pVm,
            kOverflowSuspendArray2Offset,
            0x10,
            overflowSuspend2) ||
        !TryHashCount(pVm, kAllRunningStacksOffset, result.Counts.RunningStacks) ||
        !TryHashCount(
            pVm,
            kWaitingLatentReturnsOffset,
            result.Counts.LatentReturnQueue) ||
        !TryAdd(
            linkedFunctions,
            overflowFunctions,
            result.Counts.FunctionMessageQueues) ||
        !TryAdd(suspend1, suspend2, result.Counts.SuspendResumeQueues) ||
        !TryAdd(
            result.Counts.SuspendResumeQueues,
            overflowSuspend1,
            result.Counts.SuspendResumeQueues) ||
        !TryAdd(
            result.Counts.SuspendResumeQueues,
            overflowSuspend2,
            result.Counts.SuspendResumeQueues))
    {
        result.DiagnosticStatus =
            PartyQuestSkyrimPapyrusDiagnosticStatus::LayoutValidationFailed;
        return result;
    }

    result.Counts.VmTaskQueue = vmTasks;
    result.Counts.UiWaitingQueue = *At<const uint32_t>(
        pVm,
        kUiWaitingFunctionMessagesOffset);
    if (result.Counts.UiWaitingQueue > kMaximumPlausibleDomainCount)
    {
        result.DiagnosticStatus =
            PartyQuestSkyrimPapyrusDiagnosticStatus::LayoutValidationFailed;
        return result;
    }

    const auto after = m_ingressEpoch.Capture();
    result.Observation.QuestEventGeneration = after.Generation;
    result.IngressHookInvocationCount = GetIngressHookInvocationCount();
    if (!PartyQuestPapyrusIngressEpoch::IsStable(before, after))
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
