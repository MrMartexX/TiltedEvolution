#pragma once

#include <cstdint>

enum class PartyQuestSaveKind : uint8_t
{
    Manual,
    Auto,
    Quick,
    ControlledCheckpoint
};

enum class PartyQuestSaveGuardAcquireStatus : uint8_t
{
    Acquired,
    Duplicate,
    Busy,
    InvalidTransaction
};

/**
 * Pure policy/lease model for the future Skyrim save interception layer.
 *
 * While a critical repair holds the lease, user/manual/auto/quick saves are
 * denied. A controlled checkpoint operation is still allowed so the repair
 * system can create/confirm its own guarded checkpoint. No Skyrim save hook is
 * connected in this milestone.
 */
class PartyQuestSaveGuard final
{
public:
    [[nodiscard]] PartyQuestSaveGuardAcquireStatus Acquire(uint64_t aTransactionId) noexcept;
    bool Release(uint64_t aTransactionId) noexcept;

    [[nodiscard]] bool CanSave(PartyQuestSaveKind aKind) const noexcept;
    [[nodiscard]] bool IsActive() const noexcept { return m_transactionId != 0; }
    [[nodiscard]] uint64_t GetTransactionId() const noexcept { return m_transactionId; }

private:
    uint64_t m_transactionId{};
};
