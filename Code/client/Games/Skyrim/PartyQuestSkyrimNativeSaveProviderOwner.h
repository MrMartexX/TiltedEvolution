#pragma once

#include <Games/Skyrim/PartyQuestSkyrimNativeSaveProviderResolver.h>
#include <Structs/Skyrim/PartyQuestAsyncSaveLifecycle.h>

#include <filesystem>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

enum class PartyQuestSkyrimNativeSaveProviderOwnerStatus : uint8_t
{
    Bound,
    InvalidExpectedGeneration,
    AlreadyBound,
    AdmissionClosed,
    ResolveRejected,
    StaleGeneration,
    Invalidated,
    InvalidationDeferred,
    DrainPending,
    SynchronizationFailed
};

struct PartyQuestSkyrimNativeSaveTrackedBeginResult final
{
    PartyQuestSkyrimNativeSaveProviderCommandStatus Status{
        PartyQuestSkyrimNativeSaveProviderCommandStatus::ProviderRejected};
    bool EngineInvocationAttempted{};
    bool EngineInvocationSucceeded{};
    bool NativeReservationAccepted{};
    bool DrainRequired{};
};

struct PartyQuestSkyrimNativeSaveTrackedPollResult final
{
    PartyQuestSkyrimNativeSaveProviderPollStatus Status{
        PartyQuestSkyrimNativeSaveProviderPollStatus::ProviderRejected};
    bool Retired{};
    std::optional<PartyQuestAsyncSaveCompletion> Completion;
};

struct PartyQuestSkyrimNativeSaveProviderOwnerBindResult final
{
    PartyQuestSkyrimNativeSaveProviderOwnerStatus Status{
        PartyQuestSkyrimNativeSaveProviderOwnerStatus::ResolveRejected};
    PartyQuestSkyrimNativeSaveProviderResolveStatus ResolveStatus{
        PartyQuestSkyrimNativeSaveProviderResolveStatus::ProviderUnavailable};
    uint64_t RuntimeGeneration{};

    [[nodiscard]] bool IsBound() const noexcept
    {
        return (Status == PartyQuestSkyrimNativeSaveProviderOwnerStatus::Bound ||
                   Status == PartyQuestSkyrimNativeSaveProviderOwnerStatus::
                       AlreadyBound) &&
            RuntimeGeneration != 0u;
    }
};

/**
 * Single serialized owner of the authenticated Skyrim/SKSE save capability.
 *
 * The lifecycle caller must invoke Invalidate() before beginning the matching
 * process-generation transition. Simple leaf commands hold this owner's mutex;
 * the compound engine-admission command publishes an active-operation gate
 * while executing outside it. Invalidate() first closes admission and then
 * waits for any active call to return. Acquiring the global exclusive
 * generation lease before entering Invalidate() would reverse that lock order
 * and is forbidden.
 *
 * This owner does not enable capture or register itself in production.
 * BeginTrackedAndInvoke is the only request-owning path suitable for future
 * capture wiring. It preallocates correlation state before native reservation,
 * keeps the exact request drain-owned through physical retirement, and defers
 * same-thread revocation while the compound native/engine call is active. The
 * exact generation lease is held by the capability across reservation and
 * engine admission.
 */
class PartyQuestSkyrimNativeSaveProviderOwner final
{
public:
    PartyQuestSkyrimNativeSaveProviderOwner() noexcept = default;
    ~PartyQuestSkyrimNativeSaveProviderOwner() noexcept;

    PartyQuestSkyrimNativeSaveProviderOwner(
        const PartyQuestSkyrimNativeSaveProviderOwner&) = delete;
    PartyQuestSkyrimNativeSaveProviderOwner& operator=(
        const PartyQuestSkyrimNativeSaveProviderOwner&) = delete;
    PartyQuestSkyrimNativeSaveProviderOwner(
        PartyQuestSkyrimNativeSaveProviderOwner&&) = delete;
    PartyQuestSkyrimNativeSaveProviderOwner& operator=(
        PartyQuestSkyrimNativeSaveProviderOwner&&) = delete;

    [[nodiscard]] PartyQuestSkyrimNativeSaveProviderOwnerBindResult Bind(
        const std::filesystem::path& acTrustedGameDirectory,
        uint64_t aExpectedGeneration) noexcept;

    [[nodiscard]] PartyQuestSkyrimNativeSaveProviderCommandStatus Begin(
        const PartyQuestAsyncSaveRequestIdentity& acIdentity) noexcept;
    [[nodiscard]] PartyQuestSkyrimNativeSaveProviderBeginInvokeResult
    BeginAndInvoke(
        const PartyQuestAsyncSaveRequestIdentity& acIdentity,
        PartyQuestSkyrimNativeSaveInvoker apInvoker,
        void* apContext) noexcept;
    [[nodiscard]] PartyQuestSkyrimNativeSaveTrackedBeginResult
    BeginTrackedAndInvoke(
        const PartyQuestAsyncSaveRequestIdentity& acIdentity,
        uint64_t aNowMs,
        bool aMainPathExists,
        bool aCosavePathExists,
        PartyQuestSkyrimNativeSaveInvoker apInvoker,
        void* apContext) noexcept;
    // Polling remains available after admission closes. A completion is
    // released only from the exact authenticated retirement event and only
    // while the tracked lifecycle still authorizes publication.
    [[nodiscard]] PartyQuestSkyrimNativeSaveTrackedPollResult
    PollTracked(uint64_t aNowMs) noexcept;
    // Revokes logical authority immediately and requests native cancellation,
    // but deliberately retains the capability until exact retirement.
    [[nodiscard]] PartyQuestSkyrimNativeSaveProviderOwnerStatus
    CloseAdmissionAndCancelForDrain() noexcept;
    [[nodiscard]] PartyQuestSkyrimNativeSaveProviderCommandStatus Cancel(
        uint64_t aAttemptNonce) noexcept;
    [[nodiscard]] PartyQuestSkyrimNativeSaveProviderPollResult PollAndRoute(
        const PartyQuestAsyncSaveRequestIdentity& acReservedIdentity,
        PartyQuestAsyncSaveContract& aContract,
        PartyQuestAsyncSaveFinalizationGate& aGate,
        uint64_t aNowMs) noexcept;

    [[nodiscard]] PartyQuestSkyrimNativeSaveProviderOwnerStatus
    Invalidate() noexcept;
    // DrainPending is a hard P0-C handoff requirement: the owner must remain
    // alive and PollTracked must continue until retirement before destruction.
    PartyQuestSkyrimNativeSaveProviderOwnerStatus Shutdown() noexcept;

    [[nodiscard]] bool IsBound() const noexcept;
    [[nodiscard]] bool IsShutdown() const noexcept;
    [[nodiscard]] uint64_t GetRuntimeGeneration() const noexcept;

private:
    struct TrackedRequest final
    {
        explicit TrackedRequest(PartyQuestAsyncSaveRequestIdentity aIdentity)
            : Identity(std::move(aIdentity))
        {
        }

        PartyQuestAsyncSaveRequestIdentity Identity;
        PartyQuestAsyncSaveContract Contract;
        PartyQuestAsyncSaveFinalizationGate Gate;
        PartyQuestAsyncSaveLifecycle Lifecycle;
    };

    [[nodiscard]] bool HasDrainLocked() const noexcept;
    void FinishActiveOperation() noexcept;
    void InvalidateLocked() noexcept;

    mutable std::mutex m_mutex;
    std::condition_variable m_operationDrained;
    PartyQuestNativeSaveProviderRegistration m_registration;
    std::optional<PartyQuestSkyrimNativeSaveProviderPollCapability> m_capability;
    std::unique_ptr<TrackedRequest> m_tracked;
    uint64_t m_runtimeGeneration{};
    bool m_accepting{};
    bool m_operationActive{};
    bool m_deferredInvalidation{};
    std::thread::id m_operationThread;
    std::atomic_bool m_revoking{true};
    std::atomic_bool m_shutdown{false};
};
