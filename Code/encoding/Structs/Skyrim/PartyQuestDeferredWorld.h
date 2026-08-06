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
 * External runtime hooks must explicitly call MarkReady only after resolving all
 * targets for the queued transaction. Newer canonical revisions replace older
 * pending work for the same quest so stale repairs are never executed later.
 *
 * A deferred request also needs the exact compatibility-bound runtime mutation
 * authorization carried by its apply plan. This is defense in depth: the
 * RuntimeApply coordinator validates the same proof again before admitting the
 * transaction.
 */
class PartyQuestDeferredWorldQueue final
{
public:
    [[nodiscard]] PartyQuestDeferredWorldEnqueueStatus Enqueue(
        PartyQuestRuntimeApplyRequest aRequest);

    bool MarkReady(uint64_t aTransactionId) noexcept;

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

private:
    [[nodiscard]] static uint64_t ComputeRequestFingerprint(
        const PartyQuestRuntimeApplyRequest& acRequest) noexcept;
    [[nodiscard]] static std::vector<GameId> CollectWorldTargets(
        const QuestSnapshot& acSnapshot);

    std::unordered_map<GameId, PartyQuestDeferredWorldEntry> m_entries;
    std::unordered_map<uint64_t, GameId> m_transactionQuests;
    std::unordered_map<uint64_t, uint64_t> m_transactionFingerprints;
};
