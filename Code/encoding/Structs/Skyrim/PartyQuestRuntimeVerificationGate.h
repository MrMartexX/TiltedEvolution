#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeCompatibility.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>

#include <functional>
#include <optional>

/**
 * Point-of-use post-mutation verification gate.
 *
 * Production verification must not accept a caller-supplied snapshot together
 * with the compatibility fingerprint copied from the expected envelope. This
 * gate first proves that the exact process runtime owner, guarded session,
 * durable session and process SaveGuard are one identity domain. Structural
 * mismatch returns InvalidState without observing runtime state, installing a
 * capability or applying fail-closed recovery to either session.
 *
 * Once structural identity is proven, the gate pins the shared process runtime
 * generation, obtains both the current QuestSnapshot and current compatibility
 * facts while that generation cannot be invalidated, evaluates compatibility,
 * installs a one-shot process-local capability in the durable session, and
 * immediately consumes it through the guarded verification path.
 *
 * No reusable authorization escapes the call. Missing/rejected observations
 * inside the proven identity domain are submitted as invalid verification to the
 * existing bounded guard, which after the mutation barrier fails closed into
 * exact PreRepair recovery.
 */
class PartyQuestRuntimeVerificationGate final
{
public:
    using SnapshotObserver = std::function<std::optional<QuestSnapshot>(
        const GameId&)>;
    using CompatibilityObserver = std::function<std::optional<PartyQuestRuntimeCompatibilityFacts>(
        const GameId&)>;

    [[nodiscard]] static PartyQuestRuntimeGuardedVerificationResult Submit(
        PartyQuestRuntimeGuardedSession& aGuardedSession,
        PartyQuestRuntimeApplySession& aSession,
        PartyQuestRuntimeVerificationMonitor& aMonitor,
        uint64_t aTransactionId,
        uint64_t aNowMs,
        const PartyQuestRuntimeCompatibilityRequirement& acRequirement,
        const SnapshotObserver& acSnapshotObserver,
        const CompatibilityObserver& acCompatibilityObserver) noexcept;
};
