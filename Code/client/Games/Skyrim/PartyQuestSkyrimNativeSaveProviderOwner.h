#pragma once

#include <Games/Skyrim/PartyQuestSkyrimNativeSaveProviderResolver.h>

#include <filesystem>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

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
    SynchronizationFailed
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
 * BeginAndInvoke is the sole safe reservation-to-engine-admission boundary:
 * it marks one active operation, executes outside the state mutex, and defers
 * same-thread revocation until that operation returns. The exact generation
 * lease is held by the capability across both calls.
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
    [[nodiscard]] PartyQuestSkyrimNativeSaveProviderCommandStatus Cancel(
        uint64_t aAttemptNonce) noexcept;
    [[nodiscard]] PartyQuestSkyrimNativeSaveProviderPollResult PollAndRoute(
        const PartyQuestAsyncSaveRequestIdentity& acReservedIdentity,
        PartyQuestAsyncSaveContract& aContract,
        PartyQuestAsyncSaveFinalizationGate& aGate,
        uint64_t aNowMs) noexcept;

    [[nodiscard]] PartyQuestSkyrimNativeSaveProviderOwnerStatus
    Invalidate() noexcept;
    void Shutdown() noexcept;

    [[nodiscard]] bool IsBound() const noexcept;
    [[nodiscard]] bool IsShutdown() const noexcept;
    [[nodiscard]] uint64_t GetRuntimeGeneration() const noexcept;

private:
    void FinishActiveOperation() noexcept;
    void InvalidateLocked() noexcept;

    mutable std::mutex m_mutex;
    std::condition_variable m_operationDrained;
    PartyQuestNativeSaveProviderRegistration m_registration;
    std::optional<PartyQuestSkyrimNativeSaveProviderPollCapability> m_capability;
    uint64_t m_runtimeGeneration{};
    bool m_accepting{};
    bool m_operationActive{};
    bool m_deferredInvalidation{};
    std::thread::id m_operationThread;
    std::atomic_bool m_revoking{true};
    std::atomic_bool m_shutdown{false};
};
