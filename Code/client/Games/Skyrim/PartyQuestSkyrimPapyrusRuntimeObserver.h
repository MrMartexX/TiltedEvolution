#pragma once

#include <Structs/Skyrim/PartyQuestPapyrusIngressEpoch.h>
#include <Structs/Skyrim/PartyQuestPapyrusRuntimeMonitor.h>

#include <cstdint>

enum class PartyQuestSkyrimPapyrusDiagnosticStatus : uint8_t
{
    Sampled,
    UnsupportedRuntime,
    HooksUnavailable,
    VirtualMachineUnavailable,
    VirtualTableMismatch,
    MemoryValidationFailed,
    LockContended,
    LayoutValidationFailed,
    GenerationChanged
};

struct PartyQuestSkyrimPapyrusDomainCounts final
{
    uint32_t FunctionMessageQueues{};
    uint32_t VmTaskQueue{};
    uint32_t UiWaitingQueue{};
    uint32_t SuspendResumeQueues{};
    uint32_t RunningStacks{};
    uint32_t LatentReturnQueue{};
};

struct PartyQuestSkyrimPapyrusDiagnosticSample final
{
    PartyQuestPapyrusRuntimeObservation Observation;
    PartyQuestSkyrimPapyrusDomainCounts Counts;
    PartyQuestSkyrimPapyrusDiagnosticStatus DiagnosticStatus{
        PartyQuestSkyrimPapyrusDiagnosticStatus::VirtualMachineUnavailable};
    uint64_t IngressHookInvocationCount{};
    uint64_t ProcessGeneration{};
    bool ExactRuntimeIdentity{};
    bool IngressHooksRegistered{};
    bool VirtualTableMatched{};
};

/**
 * Read-only, fail-closed Papyrus VM observer for exact registered images.
 *
 * The layout and vtable contract is intentionally exact-version bound. Every
 * sampled pointer/range and container invariant is checked, all relevant VM
 * locks are acquired with non-blocking try-locks, and a poll-independent ingress
 * epoch is sampled around the locked read. Any mismatch returns Unknown (or
 * Unsupported for another runtime) and never falls back to unlocked/partial
 * counts.
 *
 * Authorize() issues a capability only when exact executable identity,
 * Address Library selection, hook coverage and snapshot contracts all match a
 * registered profile. Sampling alone never grants mutation authority.
 */
class PartyQuestSkyrimPapyrusRuntimeObserver final
    : public PartyQuestPapyrusRuntimeObserver
{
public:
    [[nodiscard]] static PartyQuestSkyrimPapyrusRuntimeObserver&
    GetProcessObserver() noexcept;

    [[nodiscard]] PartyQuestPapyrusRuntimeObservation Observe(
        uint64_t aTransactionId) noexcept override;

    [[nodiscard]] PartyQuestSkyrimPapyrusDiagnosticSample
    SampleDiagnostics() noexcept;

    /** Exact-profile capability; invalid for unsupported/unproven images. */
    [[nodiscard]] PartyQuestPapyrusRuntimeObserverAuthorization
    Authorize() noexcept;

    [[nodiscard]] static const char* DiagnosticStatusName(
        PartyQuestSkyrimPapyrusDiagnosticStatus aStatus) noexcept;

    [[nodiscard]] uint64_t GetIngressHookInvocationCount() const noexcept
    {
        return m_ingressEpoch.GetIngressCount();
    }

private:
    friend class PartyQuestSkyrimPapyrusHookBridge;

    PartyQuestSkyrimPapyrusRuntimeObserver() noexcept = default;

    [[nodiscard]] PartyQuestPapyrusIngressEpoch::Scope BeginIngress() noexcept
    {
        return m_ingressEpoch.BeginIngress();
    }

    PartyQuestPapyrusIngressEpoch m_ingressEpoch;
};
