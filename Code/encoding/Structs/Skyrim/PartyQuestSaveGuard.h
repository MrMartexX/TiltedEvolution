#pragma once

#include <atomic>
#include <cstdint>
#include <shared_mutex>

class PartyQuestControlledSaveScope;
class PartyQuestEngineSavePermit;

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

struct PartyQuestEngineLoadTicket
{
    uint64_t Value{};

    [[nodiscard]] bool IsValid() const noexcept { return Value != 0; }
};

/**
 * Transaction-scoped policy/lease used by the Skyrim save/load interception
 * layer.
 *
 * Allowed engine saves hold the shared side of m_engineSaveGate for the whole
 * synchronous save call. Engine loads use an explicit cross-thread ticket:
 * BeginEngineLoad() publishes the pending lifecycle transition under the
 * exclusive side and CompleteEngineLoad() clears it only for the exact ticket.
 * Acquire() takes the same exclusive side and rejects while a load is pending.
 * This prevents a critical repair from becoming active between an asynchronous
 * LoadGame request and the engine's later load-complete event.
 */
class PartyQuestSaveGuard final
{
public:
    [[nodiscard]] static PartyQuestSaveGuard& GetProcessGuard() noexcept;

    [[nodiscard]] PartyQuestSaveGuardAcquireStatus Acquire(uint64_t aTransactionId) noexcept;
    bool Release(uint64_t aTransactionId) noexcept;

    [[nodiscard]] bool CanSave(PartyQuestSaveKind aKind) const noexcept;

    /**
     * Acquire permission to call the real Skyrim save function. The returned
     * permit must stay alive until the original engine call returns.
     *
     * Inactive guard: ordinary save is allowed.
     * Active guard: only an exact transaction-bound controlled scope on this
     * thread is allowed. Lock acquisition failure fails closed.
     */
    [[nodiscard]] PartyQuestEngineSavePermit TryEnterEngineSave() const noexcept;

    /**
     * Publish an engine LoadGame transition before calling Load_Impl.
     *
     * No controlled-repair bypass exists. The request is denied while a
     * critical repair or another load transition is active. A valid ticket must
     * remain published until the exact load-complete event (or a proven failed
     * request) calls CompleteEngineLoad().
     */
    [[nodiscard]] PartyQuestEngineLoadTicket BeginEngineLoad() noexcept;
    [[nodiscard]] bool CompleteEngineLoad(PartyQuestEngineLoadTicket aTicket) noexcept;
    [[nodiscard]] bool IsEngineLoadPending() const noexcept;

    [[nodiscard]] bool IsActive() const noexcept
    {
        return GetTransactionId() != 0;
    }

    [[nodiscard]] uint64_t GetTransactionId() const noexcept
    {
        return m_transactionId.load(std::memory_order_acquire);
    }

private:
    [[nodiscard]] uint64_t AllocateEngineLoadTicketLocked() noexcept;

    mutable std::shared_mutex m_engineSaveGate;
    std::atomic<uint64_t> m_transactionId{};
    uint64_t m_engineLoadTicket{};
    uint64_t m_nextEngineLoadTicket{1};

    friend class PartyQuestEngineSavePermit;
};

/**
 * RAII permit held across the complete original BGSSaveLoadManager save call.
 * A denied/default permit owns no shared lock.
 */
class PartyQuestEngineSavePermit final
{
public:
    PartyQuestEngineSavePermit() noexcept = default;
    ~PartyQuestEngineSavePermit() = default;

    PartyQuestEngineSavePermit(const PartyQuestEngineSavePermit&) = delete;
    PartyQuestEngineSavePermit& operator=(const PartyQuestEngineSavePermit&) = delete;
    PartyQuestEngineSavePermit(PartyQuestEngineSavePermit&&) noexcept = default;
    PartyQuestEngineSavePermit& operator=(PartyQuestEngineSavePermit&&) noexcept = default;

    [[nodiscard]] bool IsAllowed() const noexcept { return m_allowed; }

private:
    explicit PartyQuestEngineSavePermit(const PartyQuestSaveGuard& acGuard);

    std::shared_lock<std::shared_mutex> m_lock;
    bool m_allowed{};

    friend class PartyQuestSaveGuard;
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