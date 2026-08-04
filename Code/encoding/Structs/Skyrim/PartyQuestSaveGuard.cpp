#include <Structs/Skyrim/PartyQuestSaveGuard.h>

thread_local PartyQuestSaveGuard* PartyQuestControlledSaveScope::s_pGuard = nullptr;
thread_local uint64_t PartyQuestControlledSaveScope::s_transactionId = 0;
thread_local uint32_t PartyQuestControlledSaveScope::s_depth = 0;

PartyQuestSaveGuard& PartyQuestSaveGuard::GetProcessGuard() noexcept
{
    static PartyQuestSaveGuard guard;
    return guard;
}

PartyQuestSaveGuardAcquireStatus PartyQuestSaveGuard::Acquire(uint64_t aTransactionId) noexcept
{
    if (aTransactionId == 0)
        return PartyQuestSaveGuardAcquireStatus::InvalidTransaction;

    uint64_t expected = 0;
    if (m_transactionId.compare_exchange_strong(
            expected,
            aTransactionId,
            std::memory_order_acq_rel,
            std::memory_order_acquire))
    {
        return PartyQuestSaveGuardAcquireStatus::Acquired;
    }

    return expected == aTransactionId
        ? PartyQuestSaveGuardAcquireStatus::Duplicate
        : PartyQuestSaveGuardAcquireStatus::Busy;
}

bool PartyQuestSaveGuard::Release(uint64_t aTransactionId) noexcept
{
    if (aTransactionId == 0)
        return false;

    uint64_t expected = aTransactionId;
    return m_transactionId.compare_exchange_strong(
        expected,
        0,
        std::memory_order_acq_rel,
        std::memory_order_acquire);
}

bool PartyQuestSaveGuard::CanSave(PartyQuestSaveKind aKind) const noexcept
{
    if (!IsActive())
        return true;

    return aKind == PartyQuestSaveKind::ControlledCheckpoint;
}

bool PartyQuestSaveGuard::CanEnterEngineSave() const noexcept
{
    if (!IsActive())
        return true;

    return CanSave(PartyQuestSaveKind::ControlledCheckpoint) &&
        PartyQuestControlledSaveScope::IsAuthorized(*this);
}

PartyQuestControlledSaveScope::PartyQuestControlledSaveScope(
    PartyQuestSaveGuard& aGuard,
    uint64_t aTransactionId) noexcept
    : m_pGuard(&aGuard)
    , m_transactionId(aTransactionId)
{
    if (aTransactionId == 0 || aGuard.GetTransactionId() != aTransactionId)
        return;

    if (s_depth == 0)
    {
        s_pGuard = &aGuard;
        s_transactionId = aTransactionId;
        s_depth = 1;
        m_armed = true;
        return;
    }

    if (s_pGuard == &aGuard && s_transactionId == aTransactionId)
    {
        ++s_depth;
        m_armed = true;
    }
}

PartyQuestControlledSaveScope::~PartyQuestControlledSaveScope()
{
    if (!m_armed || s_depth == 0 || s_pGuard != m_pGuard ||
        s_transactionId != m_transactionId)
    {
        return;
    }

    --s_depth;
    if (s_depth == 0)
    {
        s_pGuard = nullptr;
        s_transactionId = 0;
    }
}

bool PartyQuestControlledSaveScope::IsAuthorized(
    const PartyQuestSaveGuard& acGuard) noexcept
{
    if (s_depth == 0 || s_pGuard != &acGuard || s_transactionId == 0)
        return false;

    return acGuard.GetTransactionId() == s_transactionId;
}
