#pragma once

#include <atomic>
#include <cstdint>

class PartyQuestControlledSaveScope;

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
 * Transaction-scoped policy/lease used by the Skyrim save interception layer.
 *
 * The transaction id is atomic because the game-level save hook and the
 * runtime coordinator must remain coherent even if a save request is emitted
 * from another thread. A critical repair denies ordinary saves; a controlled
 * checkpoint is authorized separately by a thread-local, transaction-bound
 * PartyQuestControlledSaveScope.
 */
class PartyQuestSaveGuard final
{
public:
    [[nodiscard]] static PartyQuestSaveGuard& GetProcessGuard() noexcept;

    [[nodiscard]] PartyQuestSaveGuardAcquireStatus Acquire(uint64_t aTransactionId) noexcept;
    bool Release(uint64_t aTransactionId) noexcept;

    [[nodiscard]] bool CanSave(PartyQuestSaveKind aKind) const noexcept;

    /**
     * Policy queried immediately before entering the real Skyrim save
     * function. An inactive guard is transparent. An active guard requires a
     * matching controlled-save scope on the current thread.
     */
    [[nodiscard]] bool CanEnterEngineSave() const noexcept;

    [[nodiscard]] bool IsActive() const noexcept
    {
        return GetTransactionId() != 0;
    }

    [[nodiscard]] uint64_t GetTransactionId() const noexcept
    {
        return m_transactionId.load(std::memory_order_acquire);
    }

private:
    std::atomic<uint64_t> m_transactionId{};
};

/**
 * Narrow bypass for a system-created checkpoint save.
 *
 * The scope arms only when its transaction id matches the currently held
 * guard. Nested scopes are valid only for the same guard/transaction pair.
 * Authorization is thread-local, so another thread cannot inherit the bypass.
 */
class PartyQuestControlledSaveScope final
{
public:
    PartyQuestControlledSaveScope(
        PartyQuestSaveGuard& aGuard,
        uint64_t aTransactionId) noexcept;
    ~PartyQuestControlledSaveScope();

    PartyQuestControlledSaveScope(const PartyQuestControlledSaveScope&) = delete;
    PartyQuestControlledSaveScope& operator=(const PartyQuestControlledSaveScope&) = delete;
    PartyQuestControlledSaveScope(PartyQuestControlledSaveScope&&) = delete;
    PartyQuestControlledSaveScope& operator=(PartyQuestControlledSaveScope&&) = delete;

    [[nodiscard]] bool IsArmed() const noexcept { return m_armed; }

    [[nodiscard]] static bool IsAuthorized(
        const PartyQuestSaveGuard& acGuard) noexcept;

private:
    PartyQuestSaveGuard* m_pGuard{};
    uint64_t m_transactionId{};
    bool m_armed{};

    static thread_local PartyQuestSaveGuard* s_pGuard;
    static thread_local uint64_t s_transactionId;
    static thread_local uint32_t s_depth;
};
