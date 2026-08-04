#include <Structs/Skyrim/PartyQuestSaveGuard.h>

#include <mutex>

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

    try
    {
        // Exclusive acquisition waits for any engine save that already entered
        // under a shared permit before publishing the critical repair lease.
        std::unique_lock lock(m_engineSaveGate);
        const uint64_t current = m_transactionId.load(std::memory_order_acquire);
        if (current == aTransactionId)
            return PartyQuestSaveGuardAcquireStatus::Duplicate;
        if (current != 0)
            return PartyQuestSaveGuardAcquireStatus::Busy;

        m_transactionId.store(aTransactionId, std::memory_order_release);
        return PartyQuestSaveGuardAcquireStatus::Acquired;
    }
    catch (...)
    {
        // Locking failure must never publish a repair that the save hook cannot
        // reliably guard.
        return PartyQuestSaveGuardAcquireStatus::Busy;
    }
}

bool PartyQuestSaveGuard::Release(uint64_t aTransactionId) noexcept
{
    if (aTransactionId == 0)
        return false;

    try
    {
        // The exclusive side also waits for a controlled checkpoint save that
        // is still executing under its shared engine permit.
        std::unique_lock lock(m_engineSaveGate);
        if (m_transactionId.load(std::memory_order_acquire) != aTransactionId)
            return false;

        m_transactionId.store(0, std::memory_order_release);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool PartyQuestSaveGuard::CanSave(PartyQuestSaveKind aKind) const noexcept
{
    if (!IsActive())
        return true;

    return aKind == PartyQuestSaveKind::ControlledCheckpoint;
}

PartyQuestEngineSavePermit PartyQuestSaveGuard::TryEnterEngineSave() const noexcept
{
    try
    {
        return PartyQuestEngineSavePermit(*this);
    }
    catch (...)
    {
        return {};
    }
}

PartyQuestEngineSavePermit::PartyQuestEngineSavePermit(
    const PartyQuestSaveGuard& acGuard)
    : m_lock(acGuard.m_engineSaveGate)
{
    const uint64_t transactionId = acGuard.GetTransactionId();
    if (transactionId == 0)
    {
        m_allowed = true;
        return;
    }

    if (PartyQuestControlledSaveScope::IsAuthorized(acGuard))
    {
        m_allowed = true;
        return;
    }

    // Do not hold a shared lock for a denied request. The critical repair owns
    // the logical lease already; returning false is enough to stop this save.
    m_lock.unlock();
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
