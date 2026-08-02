#pragma once

#include <Structs/Skyrim/QuestSnapshot.h>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

/**
 * One proposed canonical quest-state transition.
 *
 * TransactionId is campaign-wide and must never be reused for different
 * payloads. ExpectedQuestRevision provides optimistic concurrency control.
 */
struct PartyQuestTransaction
{
    uint64_t TransactionId{};
    uint32_t InitiatorPlayerId{};
    GameId QuestId{};
    uint64_t ExpectedQuestRevision{};
    QuestSnapshot ProposedSnapshot;

    /** Compares semantic canonical payloads, not container insertion order. */
    bool operator==(const PartyQuestTransaction& acRhs) const;
};

/** One accepted transaction as recorded in the campaign event journal. */
struct PartyQuestJournalEntry
{
    uint64_t WorldRevision{};
    uint64_t QuestRevision{};
    PartyQuestTransaction Transaction;

    bool operator==(const PartyQuestJournalEntry&) const = default;
};

enum class PartyQuestApplyStatus : uint8_t
{
    Accepted,
    Duplicate,
    InvalidTransactionId,
    QuestIdMismatch,
    RevisionMismatch,
    TransactionConflict,
    AdmissionRejected
};

struct PartyQuestApplyResult
{
    PartyQuestApplyStatus Status{PartyQuestApplyStatus::InvalidTransactionId};
    uint64_t WorldRevision{};
    uint64_t QuestRevision{};

    bool operator==(const PartyQuestApplyResult&) const noexcept = default;
};

/**
 * Game-independent canonical quest state for one cooperative campaign.
 *
 * This first store deliberately does not apply Skyrim runtime mutations. It
 * validates revisions, makes accepted operations idempotent, assigns canonical
 * revisions, and records an append-only journal suitable for deterministic
 * replay and later persistence.
 */
class PartyQuestState final
{
public:
    [[nodiscard]] PartyQuestApplyResult Apply(const PartyQuestTransaction& acTransaction);

    [[nodiscard]] const QuestSnapshot* FindQuest(const GameId& acQuestId) const noexcept;
    [[nodiscard]] uint64_t GetWorldRevision() const noexcept { return m_worldRevision; }
    [[nodiscard]] size_t GetQuestCount() const noexcept { return m_quests.size(); }
    [[nodiscard]] const std::unordered_map<GameId, QuestSnapshot>& GetQuests() const noexcept { return m_quests; }
    [[nodiscard]] const std::vector<PartyQuestJournalEntry>& GetJournal() const noexcept { return m_journal; }

private:
    uint64_t m_worldRevision{};
    std::unordered_map<GameId, QuestSnapshot> m_quests;
    std::unordered_map<uint64_t, size_t> m_transactionJournalIndices;
    std::vector<PartyQuestJournalEntry> m_journal;
};
