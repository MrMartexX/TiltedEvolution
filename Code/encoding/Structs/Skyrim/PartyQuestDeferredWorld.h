#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeApply.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

class PartyQuestRuntimeGenerationFence;
class PartyQuestRuntimeReferenceReadiness;

enum class PartyQuestDeferredWorldEnqueueStatus : uint8_t
{
    Queued,
    ReplacedOlderQuestRevision,
    Duplicate,
    Stale,
    TransactionConflict,
    ResourceLimitExceeded,
    InvalidRequest,
    UnsafePlan,
    NotDeferred
};

enum class PartyQuestDeferredWorldRuntimeReadinessStatus : uint8_t
{
    Ready,
    InvalidRequest,
    NotRuntimePlan,
    StaleRevision,
    IdentityMismatch,
    RuntimeGenerationChanged,
    ReferenceReadinessDomainMismatch,
    ReferenceMappingUnavailable,
    ReferenceNotReady,
    LocationReadinessUnavailable,
    LocationNotReady,
    SceneReadinessUnavailable,
    SceneNotReady,
    ObserverFailure
};

struct PartyQuestDeferredWorldRuntimeReadinessSources
{
    /** Canonical GameId -> local Skyrim FormID. Zero means unresolved. */
    std::function<uint32_t(const GameId&)> ResolveReferenceFormId;

    /** Authoritative current location readiness. Required when locations exist. */
    std::function<bool(const GameId&)> IsLocationReady;

    /** Authoritative current scene-participant readiness. Required when present. */
    std::function<bool(uint32_t)> IsSceneReady;
};

struct PartyQuestDeferredWorldRuntimeReadinessResult
{
    PartyQuestDeferredWorldRuntimeReadinessStatus Status{
        PartyQuestDeferredWorldRuntimeReadinessStatus::InvalidRequest};
    uint64_t RuntimeGeneration{};

    [[nodiscard]] bool IsReady() const noexcept
    {
        return Status == PartyQuestDeferredWorldRuntimeReadinessStatus::Ready &&
            RuntimeGeneration != 0;
    }
};

struct PartyQuestDeferredWorldEntry
{
    PartyQuestRuntimeApplyRequest Request;

    /** Concrete references proven by TESObjectLoadedEvent-compatible evidence. */
    std::vector<GameId> ReferenceTargets;

    /** Location aliases require a distinct authoritative readiness source. */
    std::vector<GameId> LocationTargets;

    /**
     * Combined diagnostic view retained for existing tooling/tests only.
     * Runtime readiness must never treat this mixed set as reference evidence.
     */
    std::vector<GameId> ReferencedWorldTargets;

    bool HasSceneDependency{};
    bool Ready{};
    uint64_t ReadyGeneration{};

    bool operator==(const PartyQuestDeferredWorldEntry&) const = default;
};

/**
 * Client-local queue for canonical repairs that cannot run until the relevant
 * world/cell/session state is available.
 *
 * Diagnostic DryRunOnly plans may use MarkReady/TakeReady. Side-effecting
 * runtime plans are deliberately excluded from that legacy surface and must use
 * TryMarkRuntimeReady/TakeRuntimeReady, which classify reference/location/scene
 * dependencies separately, bind reference mapping to the runtime generation,
 * and revalidate the latest canonical revision before release.
 *
 * A deferred request also needs the exact compatibility-bound runtime mutation
 * authorization carried by its apply plan. Pending work and remembered
 * transaction identities have local immutable ceilings; reaching either ceiling
 * fails closed until lifecycle replacement constructs a fresh campaign queue.
 */
class PartyQuestDeferredWorldQueue final
{
public:
    static constexpr size_t MaxPendingEntries = 256;
    static constexpr size_t MaxRememberedTransactions = 4096;

    using CanonicalRevisionObserver = std::function<uint64_t(const GameId&)>;

    [[nodiscard]] PartyQuestDeferredWorldEnqueueStatus Enqueue(
        PartyQuestRuntimeApplyRequest aRequest);

    /** Diagnostic DryRunOnly readiness surface. Never accepts runtime plans. */
    bool MarkReady(
        PartyQuestRuntimeApplyRequest aCurrentRequest,
        uint64_t aCurrentCanonicalQuestRevision) noexcept;

    /**
     * Runtime readiness gate. The current canonical revision is supplied
     * independently from the deferred request. Every dependency class present
     * in the queued snapshot must have its own authoritative evidence source.
     */
    [[nodiscard]] PartyQuestDeferredWorldRuntimeReadinessResult TryMarkRuntimeReady(
        PartyQuestRuntimeApplyRequest aCurrentRequest,
        uint64_t aCurrentCanonicalQuestRevision,
        PartyQuestRuntimeGenerationFence& aGenerationFence,
        const PartyQuestRuntimeReferenceReadiness& acReferenceReadiness,
        const PartyQuestDeferredWorldRuntimeReadinessSources& acSources) noexcept;

    /** Drops a queued repair if a newer canonical quest revision is already known. */
    bool InvalidateIfOlder(
        const GameId& acQuestId,
        uint64_t aCanonicalQuestRevision) noexcept;

    /** Diagnostic DryRunOnly extraction. Runtime plans are never emitted here. */
    [[nodiscard]] std::vector<PartyQuestRuntimeApplyRequest> TakeReady();

    /**
     * Runtime extraction revalidates canonical revision, generation and every
     * world dependency at point of use. A generation change clears readiness
     * and requires a fresh TryMarkRuntimeReady call.
     */
    [[nodiscard]] std::vector<PartyQuestRuntimeApplyRequest> TakeRuntimeReady(
        PartyQuestRuntimeGenerationFence& aGenerationFence,
        const PartyQuestRuntimeReferenceReadiness& acReferenceReadiness,
        const PartyQuestDeferredWorldRuntimeReadinessSources& acSources,
        const CanonicalRevisionObserver& acCanonicalRevisionObserver) noexcept;

    [[nodiscard]] const PartyQuestDeferredWorldEntry* FindByQuest(
        const GameId& acQuestId) const noexcept;

    [[nodiscard]] const PartyQuestDeferredWorldEntry* FindByTransaction(
        uint64_t aTransactionId) const noexcept;

    [[nodiscard]] size_t GetPendingCount() const noexcept { return m_entries.size(); }
    [[nodiscard]] size_t GetRememberedTransactionCount() const noexcept
    {
        return m_transactionFingerprints.size();
    }

private:
    [[nodiscard]] static std::vector<GameId> CollectReferenceTargets(
        const QuestSnapshot& acSnapshot);
    [[nodiscard]] static std::vector<GameId> CollectLocationTargets(
        const QuestSnapshot& acSnapshot);
    [[nodiscard]] static std::vector<GameId> CollectWorldTargets(
        const QuestSnapshot& acSnapshot);

    [[nodiscard]] static PartyQuestDeferredWorldRuntimeReadinessResult
    EvaluateRuntimeReadiness(
        const PartyQuestDeferredWorldEntry& acEntry,
        PartyQuestRuntimeGenerationFence& aGenerationFence,
        const PartyQuestRuntimeReferenceReadiness& acReferenceReadiness,
        const PartyQuestDeferredWorldRuntimeReadinessSources& acSources) noexcept;

    std::unordered_map<GameId, PartyQuestDeferredWorldEntry> m_entries;
    std::unordered_map<uint64_t, GameId> m_transactionQuests;
    std::unordered_map<uint64_t, PartyQuestRuntimeApplyIdentity> m_transactionFingerprints;
};
