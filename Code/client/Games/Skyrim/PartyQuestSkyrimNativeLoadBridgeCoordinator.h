#pragma once

#include <Games/Skyrim/PartyQuestSkyrimNativeLoadBridgeOwner.h>
#include <Games/Skyrim/PartyQuestSkyrimNativeLoadBridgeProvider.h>

#include <cstdint>
#include <type_traits>

class PartyQuestSkyrimNativeLoadBridgeCoordinatorTestAccess;

enum class PartyQuestSkyrimNativeLoadBridgeCoordinatorStatus : uint8_t
{
    Bypassed = 1u,
    Reserved = 2u,
    GenerationObserved = 3u,
    TargetEntered = 4u,
    Completed = 5u,
    Cancelled = 6u,
    Rejected = 7u,
    PoisonedUnsafeToUnload = 8u
};

struct PartyQuestSkyrimNativeLoadBridgeAttempt final
{
    uint64_t AttemptNonce{};
    uint64_t ReservedGeneration{};
    PartyQuestNativeLoadBridgeIdentityV1 Identity;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return AttemptNonce != 0u &&
            ReservedGeneration != 0u &&
            PartyQuestNativeLoadBridgePolicy::IsValidIdentity(Identity);
    }
};

struct PartyQuestSkyrimNativeLoadBridgeCoordinatorResult final
{
    PartyQuestSkyrimNativeLoadBridgeCoordinatorStatus Status{
        PartyQuestSkyrimNativeLoadBridgeCoordinatorStatus::Rejected};
    PartyQuestNativeLoadBridgeStatus ProviderStatus{
        PartyQuestNativeLoadBridgeStatus::BridgeUnavailable};
    uint8_t HasCompletion{};
    uint8_t Reserved[5]{};

    PartyQuestSkyrimNativeLoadBridgeAttempt Attempt;
    PartyQuestSkyrimNativeLoadBridgeOwnerResult Owner;
    PartyQuestNativeLoadBridgeCompletionV1 Completion;

    [[nodiscard]] bool IsBypassed() const noexcept
    {
        return Status ==
            PartyQuestSkyrimNativeLoadBridgeCoordinatorStatus::Bypassed;
    }

    [[nodiscard]] bool IsCompleted() const noexcept
    {
        return Status ==
                PartyQuestSkyrimNativeLoadBridgeCoordinatorStatus::Completed &&
            HasCompletion == 1u &&
            PartyQuestNativeLoadBridgePolicy::IsValidCompletion(Completion);
    }
};

/**
 * Stateless orchestration between the authenticated client owner and the
 * process-image provider for one concrete Skyrim Load_Impl attempt.
 *
 * No production feature is enabled by this class. While the process provider
 * is NotReady, Begin() returns Bypassed and performs no owner/provider work.
 * Once the provider has explicitly been published Ready, a missing exact Bound
 * owner is fail-closed Rejected instead of silently allowing an untracked load.
 *
 * Intended future hook order:
 *   Begin(actual identity) while current generation is still valid
 *   -> existing lifecycle generation transition(s)
 *   -> ObserveGeneration() for every published generation
 *   -> EnterTarget() immediately before original Load_Impl
 *   -> original target
 *   -> CompleteTarget(bool) immediately on return
 *
 * Completion synchronously proves provider completion through Owner::Poll and
 * exact Owner::Retire. It does not complete the separate TESLoadGameEvent
 * lifecycle ticket.
 *
 * The coordinator stores no request state. The stack-owned Attempt is only
 * correlation evidence; authority remains exclusively in owner/provider state.
 */
class PartyQuestSkyrimNativeLoadBridgeCoordinator final
{
public:
    PartyQuestSkyrimNativeLoadBridgeCoordinator(
        const PartyQuestSkyrimNativeLoadBridgeCoordinator&) = delete;
    PartyQuestSkyrimNativeLoadBridgeCoordinator& operator=(
        const PartyQuestSkyrimNativeLoadBridgeCoordinator&) = delete;
    PartyQuestSkyrimNativeLoadBridgeCoordinator(
        PartyQuestSkyrimNativeLoadBridgeCoordinator&&) = delete;
    PartyQuestSkyrimNativeLoadBridgeCoordinator& operator=(
        PartyQuestSkyrimNativeLoadBridgeCoordinator&&) = delete;

    [[nodiscard]] static PartyQuestSkyrimNativeLoadBridgeCoordinator&
    GetProcessCoordinator() noexcept;

    [[nodiscard]] PartyQuestSkyrimNativeLoadBridgeCoordinatorResult Begin(
        const PartyQuestNativeLoadIdentity& acIdentity) noexcept;

    [[nodiscard]] PartyQuestSkyrimNativeLoadBridgeCoordinatorResult
    ObserveGeneration(
        const PartyQuestSkyrimNativeLoadBridgeAttempt& acAttempt,
        uint64_t aGeneration) noexcept;

    [[nodiscard]] PartyQuestSkyrimNativeLoadBridgeCoordinatorResult
    EnterTarget(
        const PartyQuestSkyrimNativeLoadBridgeAttempt& acAttempt,
        const PartyQuestNativeLoadIdentity& acActualIdentity) noexcept;

    [[nodiscard]] PartyQuestSkyrimNativeLoadBridgeCoordinatorResult
    CompleteTarget(
        const PartyQuestSkyrimNativeLoadBridgeAttempt& acAttempt,
        bool aResult) noexcept;

    [[nodiscard]] PartyQuestSkyrimNativeLoadBridgeCoordinatorResult
    CancelBeforeTarget(
        const PartyQuestSkyrimNativeLoadBridgeAttempt& acAttempt) noexcept;

private:
    friend class PartyQuestSkyrimNativeLoadBridgeCoordinatorTestAccess;

    PartyQuestSkyrimNativeLoadBridgeCoordinator(
        PartyQuestSkyrimNativeLoadBridgeOwner& aOwner,
        PartyQuestSkyrimNativeLoadBridgeProvider& aProvider) noexcept
        : m_owner(aOwner)
        , m_provider(aProvider)
    {
    }

    [[nodiscard]] static PartyQuestNativeLoadBridgeIdentityV1
    ToBridgeIdentity(const PartyQuestNativeLoadIdentity& acIdentity) noexcept;

    [[nodiscard]] bool AttemptMatchesCurrent(
        const PartyQuestSkyrimNativeLoadBridgeAttempt& acAttempt) noexcept;

    [[nodiscard]] PartyQuestSkyrimNativeLoadBridgeCoordinatorResult
    PropagateUnsafe(
        const PartyQuestSkyrimNativeLoadBridgeAttempt& acAttempt,
        PartyQuestNativeLoadBridgeStatus aProviderStatus) noexcept;

    PartyQuestSkyrimNativeLoadBridgeOwner& m_owner;
    PartyQuestSkyrimNativeLoadBridgeProvider& m_provider;
};

static_assert(sizeof(
    PartyQuestSkyrimNativeLoadBridgeCoordinatorStatus) == 1u);
static_assert(std::is_standard_layout_v<
    PartyQuestSkyrimNativeLoadBridgeAttempt>);
static_assert(std::is_trivially_copyable_v<
    PartyQuestSkyrimNativeLoadBridgeAttempt>);
static_assert(std::is_standard_layout_v<
    PartyQuestSkyrimNativeLoadBridgeCoordinatorResult>);
static_assert(std::is_trivially_copyable_v<
    PartyQuestSkyrimNativeLoadBridgeCoordinatorResult>);
