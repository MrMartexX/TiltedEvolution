#pragma once

#include <Games/Skyrim/PartyQuestSkyrimNativeSaveProviderResolver.h>

#include <filesystem>
#include <atomic>
#include <mutex>
#include <optional>

enum class PartyQuestSkyrimNativeSaveProviderOwnerStatus : uint8_t
{
    Bound,
    InvalidExpectedGeneration,
    AlreadyBound,
    AdmissionClosed,
    ResolveRejected,
    StaleGeneration,
    Invalidated,
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
 * process-generation transition. Commands hold this owner's mutex through the
 * complete generation-leased native call, so Invalidate() first closes
 * admission and then waits for any active call to return. Acquiring the global
 * exclusive generation lease before entering Invalidate() would reverse that
 * lock order and is forbidden.
 *
 * This owner does not enable capture or register itself in production.
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
    mutable std::mutex m_mutex;
    PartyQuestNativeSaveProviderRegistration m_registration;
    std::optional<PartyQuestSkyrimNativeSaveProviderPollCapability> m_capability;
    uint64_t m_runtimeGeneration{};
    bool m_accepting{};
    std::atomic_bool m_revoking{true};
    std::atomic_bool m_shutdown{false};
};
