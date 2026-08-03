#include <Structs/Skyrim/PartyQuestSaveGuard.h>

PartyQuestSaveGuardAcquireStatus PartyQuestSaveGuard::Acquire(uint64_t aTransactionId) noexcept
{
    if (aTransactionId == 0)
        return PartyQuestSaveGuardAcquireStatus::InvalidTransaction;

    if (m_transactionId == aTransactionId)
        return PartyQuestSaveGuardAcquireStatus::Duplicate;

    if (m_transactionId != 0)
        return PartyQuestSaveGuardAcquireStatus::Busy;

    m_transactionId = aTransactionId;
    return PartyQuestSaveGuardAcquireStatus::Acquired;
}

bool PartyQuestSaveGuard::Release(uint64_t aTransactionId) noexcept
{
    if (aTransactionId == 0 || aTransactionId != m_transactionId)
        return false;

    m_transactionId = 0;
    return true;
}

bool PartyQuestSaveGuard::CanSave(PartyQuestSaveKind aKind) const noexcept
{
    if (!IsActive())
        return true;

    return aKind == PartyQuestSaveKind::ControlledCheckpoint;
}
