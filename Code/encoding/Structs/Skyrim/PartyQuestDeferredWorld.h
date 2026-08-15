#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeApply.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

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

struct PartyQuestDeferredWorldEntry
{
    PartyQuestRuntimeApplyRequest Request;
    std::vector<GameId> ReferencedWorldTargets;
    bool HasSceneDependency{};
    bool Ready{};

    bool operator==(const PartyQuestDeferredWorldEntry&) const = default;
};

/**
 * Client-local queue for canonical repairs that cannot run until the relevant
 * world/cell/session state is available.
 *
 * The queue never assumes that a FormId being known means its cell is loaded.
 * External runtime hooks must explicitly call MarkReady with a freshly rebuilt
 * current-canonical request after resolving all targets. The full validated
 * identity must still match the queued plan, and the caller must separately
 * supply the latest authoritative canonical quest revision. Newer canonical
 * revisions invalidate older pending work so stale repairs are never executed
 * even if an old request is presented again when a cell later becomes ready.
 *
 * A deferred request also needs the exact compatibility-bound runtime mutation
 * authorization carried by its apply plan. This is defense in depth: the
 * RuntimeApply coordinator validates the same proof again before admitting the
 * transaction. Pending work and remembered transaction identities have local
 * immutable ceilings; reaching either ceiling fails closed until lifecycle
 * replacement constructs a fresh campaign queue.
 */
class PartyQuestDeferredWorldQueue final
{
public:
    static constexpr size_t MaxPendingEntries = 256;
    static constexpr size_t MaxRememberedTransactions = 4096;

    [[nodiscard]] PartyQuestDeferredWorldEnqueueStatus Enqueue(
        PartyQuestRuntimeApplyRequest aRequest);

    /**
     * Marks one exact queued request ready only when the latest independently
     * observed canonical quest revision still equals the queued revision.
     *
     * aCurrentCanonicalQuestRevision must come from the current server-canonical
     * observation/reconcile path, not from the deferred request itself. If that
     * revision is newer, the stale queued entry is invalidated immediately.
     */
    bool MarkReady(
        PartyQuestRuntimeApplyRequest aCurrentRequest,
        uint64_t aCurrentCanonicalQuestRevision) noexcept;

    /** Drops a queued repair if a newer canonical quest revision is already known. */
    bool InvalidateIfOlder(
        const GameId& acQuestId,
        uint64_t aCanonicalQuestRevision) noexcept;

    [[nodiscard]] std::vector<PartyQuestRuntimeApplyRequest> TakeReady();

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
    [[nodiscard]] static std::vector<GameId> CollectWorldTargets(
        const QuestSnapshot& acSnapshot);

    std::unordered_map<GameId, PartyQuestDeferredWorldEntry> m_entries;
    std::unordered_map<uint64_t, GameId> m_transactionQuests;
    std::unordered_map<uint64_t, PartyQuestRuntimeApplyIdentity> m_transactionFingerprints;
};
