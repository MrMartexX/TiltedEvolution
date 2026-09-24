#pragma once

#include <condition_variable>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>

enum class PartyQuestPreLoadAdmission : uint8_t { Blocked, Allowed };

enum class PartyQuestPreLoadAdmissionRegistrationStatus : uint8_t
{
    Registered,
    InvalidCallback,
    OwnerAlreadyRegistered,
    IdentityExhausted,
    RegistryClosed
};

enum class PartyQuestPreLoadAdmissionUnregisterStatus : uint8_t
{
    Unregistered,
    StaleRegistration,
    ReentrantCallbackDeferred,
    AbandonedAttempt,
    Poisoned,
    SynchronizationFailed
};

enum class PartyQuestPreLoadAttemptStatus : uint8_t
{
    Aborted,
    Committed,
    Completed,
    StaleAttempt,
    InvalidState,
    Poisoned,
    SynchronizationFailed
};

/**
 * Process-local fail-closed bridge between Skyrim Load_Impl and its runtime
 * owner. It accepts no filename or request payload.
 *
 * Registry, Registration, and Attempt share one control block. Tokens may
 * therefore outlive the registry facade without dereferencing it. Tokens are
 * single-owner and thread-affine: callers must not invoke methods concurrently
 * on the same token.
 *
 * An attempt admitted before Unregister closes admission is grandfathered:
 * it may CommitPublished, and Unregister waits through exact Complete. This is
 * the ordering required when SaveLoad publishes its engine lifecycle tickets.
 */
class PartyQuestPreLoadAdmissionRegistry final
{
public:
    using Callback = PartyQuestPreLoadAdmission (*)(void*);

private:
    enum class AttemptPhase : uint8_t { None, Prepared, Committed, Abandoned };

    struct State final
    {
        std::mutex Mutex;
        std::condition_variable Drained;
        Callback CallbackFunction{};
        void* Context{};
        uint64_t Generation{};
        uint64_t NextAttemptId{};
        uint64_t ActiveAttemptId{};
        uint64_t ActiveCallbacks{};
        AttemptPhase Phase{AttemptPhase::None};
        bool FacadeAlive{true};
        bool Registered{};
        bool RegistrationOwned{};
        bool Accepting{};
        bool Poisoned{};
    };

public:
    class Registration final
    {
    public:
        Registration() noexcept = default;
        Registration(Registration&& aOther) noexcept
            : m_state(std::move(aOther.m_state))
            , m_generation(aOther.m_generation)
        {
            aOther.m_generation = 0u;
        }
        Registration& operator=(Registration&&) = delete;
        Registration(const Registration&) = delete;
        Registration& operator=(const Registration&) = delete;
        ~Registration() noexcept { Drop(); }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_state && m_generation != 0u;
        }

        [[nodiscard]] bool CanDestroyContext() const noexcept
        {
            return PartyQuestPreLoadAdmissionRegistry::CanDestroy(
                m_state, m_generation);
        }

        [[nodiscard]] bool IsPoisoned() const noexcept
        {
            return PartyQuestPreLoadAdmissionRegistry::Poisoned(m_state);
        }

    private:
        friend class PartyQuestPreLoadAdmissionRegistry;
        Registration(std::shared_ptr<State> aState, uint64_t aGeneration) noexcept
            : m_state(std::move(aState)), m_generation(aGeneration) {}
        void Reset() noexcept
        {
            m_state.reset();
            m_generation = 0u;
        }
        void Drop() noexcept
        {
            if (m_state)
                PartyQuestPreLoadAdmissionRegistry::DropRegistration(
                    m_state, m_generation);
            Reset();
        }
        std::shared_ptr<State> m_state;
        uint64_t m_generation{};
    };

    class Attempt final
    {
    public:
        Attempt() noexcept = default;
        Attempt(Attempt&& aOther) noexcept
            : m_state(std::move(aOther.m_state))
            , m_ownerGeneration(aOther.m_ownerGeneration)
            , m_attemptId(aOther.m_attemptId)
            , m_localPhase(aOther.m_localPhase)
        {
            aOther.Reset();
        }
        Attempt& operator=(Attempt&&) = delete;
        Attempt(const Attempt&) = delete;
        Attempt& operator=(const Attempt&) = delete;
        ~Attempt() noexcept
        {
            if (!m_state)
                return;
            if (m_localPhase == AttemptPhase::Prepared)
            {
                if (Abort() != PartyQuestPreLoadAttemptStatus::Aborted)
                    Abandon();
            }
            else if (m_localPhase == AttemptPhase::Committed)
                Abandon();
        }

        [[nodiscard]] bool IsPrepared() const noexcept
        {
            return m_state && m_attemptId != 0u &&
                m_localPhase == AttemptPhase::Prepared;
        }
        [[nodiscard]] bool IsCommitted() const noexcept
        {
            return m_state && m_attemptId != 0u &&
                m_localPhase == AttemptPhase::Committed;
        }
        [[nodiscard]] PartyQuestPreLoadAttemptStatus Abort() noexcept
        {
            return PartyQuestPreLoadAdmissionRegistry::TransitionAttempt(
                *this, AttemptPhase::Prepared, AttemptPhase::None,
                PartyQuestPreLoadAttemptStatus::Aborted);
        }
        [[nodiscard]] PartyQuestPreLoadAttemptStatus CommitPublished() noexcept
        {
            return PartyQuestPreLoadAdmissionRegistry::TransitionAttempt(
                *this, AttemptPhase::Prepared, AttemptPhase::Committed,
                PartyQuestPreLoadAttemptStatus::Committed);
        }
        [[nodiscard]] PartyQuestPreLoadAttemptStatus Complete() noexcept
        {
            return PartyQuestPreLoadAdmissionRegistry::TransitionAttempt(
                *this, AttemptPhase::Committed, AttemptPhase::None,
                PartyQuestPreLoadAttemptStatus::Completed);
        }

    private:
        friend class PartyQuestPreLoadAdmissionRegistry;
        Attempt(std::shared_ptr<State> aState, uint64_t aOwnerGeneration,
                uint64_t aAttemptId) noexcept
            : m_state(std::move(aState))
            , m_ownerGeneration(aOwnerGeneration)
            , m_attemptId(aAttemptId)
            , m_localPhase(AttemptPhase::Prepared) {}
        void Reset() noexcept
        {
            m_state.reset();
            m_ownerGeneration = 0u;
            m_attemptId = 0u;
            m_localPhase = AttemptPhase::None;
        }
        void Abandon() noexcept
        {
            PartyQuestPreLoadAdmissionRegistry::AbandonAttempt(*this);
        }
        std::shared_ptr<State> m_state;
        uint64_t m_ownerGeneration{};
        uint64_t m_attemptId{};
        AttemptPhase m_localPhase{AttemptPhase::None};
    };

    struct RegisterResult final
    {
        PartyQuestPreLoadAdmissionRegistrationStatus Status{
            PartyQuestPreLoadAdmissionRegistrationStatus::InvalidCallback};
        std::optional<Registration> Token;
    };

    struct BeginAttemptResult final
    {
        PartyQuestPreLoadAdmission Admission{PartyQuestPreLoadAdmission::Blocked};
        std::optional<Attempt> Token;
        [[nodiscard]] bool IsAllowed() const noexcept
        {
            return Admission == PartyQuestPreLoadAdmission::Allowed && Token &&
                Token->IsPrepared();
        }
    };

    PartyQuestPreLoadAdmissionRegistry()
    {
        m_state = std::make_shared<State>();
    }
    ~PartyQuestPreLoadAdmissionRegistry() noexcept { CloseFacade(m_state); }
    PartyQuestPreLoadAdmissionRegistry(
        const PartyQuestPreLoadAdmissionRegistry&) = delete;
    PartyQuestPreLoadAdmissionRegistry& operator=(
        const PartyQuestPreLoadAdmissionRegistry&) = delete;

    [[nodiscard]] RegisterResult Register(void* apContext,
                                          Callback apCallback) noexcept
    {
        RegisterResult result;
        if (!apCallback || !m_state)
            return result;
        try
        {
            std::lock_guard lock(m_state->Mutex);
            if (!m_state->FacadeAlive || m_state->Poisoned)
            {
                result.Status =
                    PartyQuestPreLoadAdmissionRegistrationStatus::RegistryClosed;
                return result;
            }
            if (m_state->Registered)
            {
                result.Status = PartyQuestPreLoadAdmissionRegistrationStatus::
                    OwnerAlreadyRegistered;
                return result;
            }
            if (m_state->Generation == std::numeric_limits<uint64_t>::max())
            {
                result.Status = PartyQuestPreLoadAdmissionRegistrationStatus::
                    IdentityExhausted;
                return result;
            }
            ++m_state->Generation;
            m_state->Context = apContext;
            m_state->CallbackFunction = apCallback;
            m_state->Registered = true;
            m_state->RegistrationOwned = true;
            m_state->Accepting = true;
            result.Token.emplace(Registration(m_state, m_state->Generation));
            result.Status = PartyQuestPreLoadAdmissionRegistrationStatus::Registered;
            return result;
        }
        catch (...) { return result; }
    }

    [[nodiscard]] PartyQuestPreLoadAdmissionUnregisterStatus Unregister(
        Registration& aRegistration) noexcept
    {
        auto state = aRegistration.m_state;
        if (!state)
            return PartyQuestPreLoadAdmissionUnregisterStatus::StaleRegistration;
        try
        {
            std::unique_lock lock(state->Mutex);
            if (!CurrentRegistration(*state, aRegistration.m_generation))
                return PartyQuestPreLoadAdmissionUnregisterStatus::StaleRegistration;
            CloseAdmission(*state);
            if (IsActiveOnThisThread(state.get()))
                return PartyQuestPreLoadAdmissionUnregisterStatus::
                    ReentrantCallbackDeferred;
            state->Drained.wait(lock, [&]() noexcept
            {
                return state->ActiveCallbacks == 0u &&
                    (state->Phase == AttemptPhase::None ||
                     state->Phase == AttemptPhase::Abandoned);
            });
            if (state->Phase == AttemptPhase::Abandoned)
                return PartyQuestPreLoadAdmissionUnregisterStatus::AbandonedAttempt;
            if (state->Poisoned)
                return PartyQuestPreLoadAdmissionUnregisterStatus::Poisoned;
            state->Registered = false;
            state->RegistrationOwned = false;
            aRegistration.Reset();
            return PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered;
        }
        catch (...)
        {
            PoisonNoThrow(state);
            return PartyQuestPreLoadAdmissionUnregisterStatus::SynchronizationFailed;
        }
    }

    [[nodiscard]] BeginAttemptResult BeginAttempt() noexcept
    {
        BeginAttemptResult result;
        auto state = m_state;
        Callback callback = nullptr;
        void* pContext = nullptr;
        uint64_t generation = 0u;
        uint64_t attemptId = 0u;
        try
        {
            std::lock_guard lock(state->Mutex);
            if (!state->FacadeAlive || state->Poisoned || !state->Registered ||
                !state->RegistrationOwned || !state->Accepting ||
                !state->CallbackFunction || state->Phase != AttemptPhase::None ||
                state->NextAttemptId == std::numeric_limits<uint64_t>::max())
                return result;
            attemptId = ++state->NextAttemptId;
            generation = state->Generation;
            state->ActiveAttemptId = attemptId;
            state->Phase = AttemptPhase::Prepared;
            callback = state->CallbackFunction;
            pContext = state->Context;
            ++state->ActiveCallbacks;
        }
        catch (...) { PoisonNoThrow(state); return result; }

        ActiveCallbackLease lease(state);
        PartyQuestPreLoadAdmission decision = PartyQuestPreLoadAdmission::Blocked;
        try { decision = callback(pContext); }
        catch (...) { decision = PartyQuestPreLoadAdmission::Blocked; }
        try
        {
            std::lock_guard lock(state->Mutex);
            const bool current = decision == PartyQuestPreLoadAdmission::Allowed &&
                state->FacadeAlive && !state->Poisoned && state->Registered &&
                state->RegistrationOwned && state->Accepting &&
                state->CallbackFunction == callback && state->Context == pContext &&
                state->Generation == generation &&
                state->ActiveAttemptId == attemptId &&
                state->Phase == AttemptPhase::Prepared;
            if (!current)
            {
                if (state->Generation == generation &&
                    state->ActiveAttemptId == attemptId &&
                    state->Phase == AttemptPhase::Prepared)
                    ClearAttempt(*state);
                return result;
            }
            result.Token.emplace(Attempt(state, generation, attemptId));
            result.Admission = PartyQuestPreLoadAdmission::Allowed;
            return result;
        }
        catch (...) { PoisonNoThrow(state); return result; }
    }

    [[nodiscard]] bool HasActiveAttempt() const noexcept
    {
        auto state = m_state;
        if (!state)
            return true;
        try
        {
            std::lock_guard lock(state->Mutex);
            return state->Phase != AttemptPhase::None;
        }
        catch (...) { return true; }
    }
    [[nodiscard]] bool CanDestroyContext() const noexcept
    {
        return CanDestroy(m_state, 0u);
    }
    [[nodiscard]] bool IsPoisoned() const noexcept { return Poisoned(m_state); }

    [[nodiscard]] static PartyQuestPreLoadAdmissionRegistry&
    GetProcessRegistry() noexcept
    {
        static PartyQuestPreLoadAdmissionRegistry s_registry;
        return s_registry;
    }

private:
    class ActiveCallbackLease final
    {
    public:
        explicit ActiveCallbackLease(std::shared_ptr<State> aState) noexcept
            : m_state(std::move(aState))
            , m_frameState(m_state.get())
            , m_previous(s_activeFrame)
        {
            s_activeFrame = this;
        }
        ~ActiveCallbackLease() noexcept
        {
            s_activeFrame = m_previous;
            try
            {
                std::lock_guard lock(m_state->Mutex);
                if (m_state->ActiveCallbacks != 0u)
                    --m_state->ActiveCallbacks;
                m_state->Drained.notify_all();
            }
            catch (...) { PoisonNoThrow(m_state); }
        }
        ActiveCallbackLease(const ActiveCallbackLease&) = delete;
        ActiveCallbackLease& operator=(const ActiveCallbackLease&) = delete;
    private:
        friend class PartyQuestPreLoadAdmissionRegistry;
        std::shared_ptr<State> m_state;
        State* m_frameState{};
        ActiveCallbackLease* m_previous{};
    };

    [[nodiscard]] static bool IsActiveOnThisThread(
        const State* apState) noexcept
    {
        for (auto* pFrame = s_activeFrame; pFrame;
             pFrame = pFrame->m_previous)
        {
            if (pFrame->m_frameState == apState)
                return true;
        }
        return false;
    }

    static bool CurrentRegistration(const State& aState,
                                    uint64_t aGeneration) noexcept
    {
        return aGeneration != 0u && aState.Registered &&
            aState.RegistrationOwned && aState.Generation == aGeneration;
    }
    static void CloseAdmission(State& aState) noexcept
    {
        aState.Accepting = false;
        aState.CallbackFunction = nullptr;
        aState.Context = nullptr;
    }
    static void ClearAttempt(State& aState) noexcept
    {
        aState.ActiveAttemptId = 0u;
        aState.Phase = AttemptPhase::None;
        aState.Drained.notify_all();
    }
    static void PoisonNoThrow(const std::shared_ptr<State>& aState) noexcept
    {
        if (!aState)
            return;
        try
        {
            std::lock_guard lock(aState->Mutex);
            aState->Poisoned = true;
            CloseAdmission(*aState);
            aState->Drained.notify_all();
        }
        catch (...) {}
    }
    static bool Poisoned(const std::shared_ptr<State>& aState) noexcept
    {
        if (!aState)
            return true;
        try
        {
            std::lock_guard lock(aState->Mutex);
            return aState->Poisoned;
        }
        catch (...) { return true; }
    }
    static bool CanDestroy(const std::shared_ptr<State>& aState,
                           uint64_t aGeneration) noexcept
    {
        if (!aState)
            return false;
        try
        {
            std::lock_guard lock(aState->Mutex);
            const bool generationMatches = aGeneration == 0u ||
                aState->Generation == aGeneration;
            return generationMatches && !aState->Poisoned &&
                !aState->Accepting && !aState->CallbackFunction &&
                aState->ActiveCallbacks == 0u &&
                aState->Phase == AttemptPhase::None;
        }
        catch (...) { return false; }
    }
    static PartyQuestPreLoadAttemptStatus TransitionAttempt(
        Attempt& aAttempt, AttemptPhase aExpected, AttemptPhase aNext,
        PartyQuestPreLoadAttemptStatus aSuccess) noexcept
    {
        auto state = aAttempt.m_state;
        if (!state || aAttempt.m_localPhase != aExpected)
            return PartyQuestPreLoadAttemptStatus::InvalidState;
        try
        {
            std::lock_guard lock(state->Mutex);
            const bool exact = state->Generation == aAttempt.m_ownerGeneration &&
                state->ActiveAttemptId == aAttempt.m_attemptId &&
                state->Phase == aExpected;
            if (!exact)
                return PartyQuestPreLoadAttemptStatus::StaleAttempt;
            if (state->Poisoned && aSuccess ==
                    PartyQuestPreLoadAttemptStatus::Committed)
                return PartyQuestPreLoadAttemptStatus::Poisoned;
            state->Phase = aNext;
            if (aNext == AttemptPhase::None)
                state->ActiveAttemptId = 0u;
            aAttempt.m_localPhase = aNext;
            if (aNext == AttemptPhase::None)
                aAttempt.Reset();
            state->Drained.notify_all();
            return aSuccess;
        }
        catch (...) { PoisonNoThrow(state); return
            PartyQuestPreLoadAttemptStatus::SynchronizationFailed; }
    }
    static void AbandonAttempt(Attempt& aAttempt) noexcept
    {
        auto state = aAttempt.m_state;
        if (!state) { aAttempt.Reset(); return; }
        try
        {
            std::lock_guard lock(state->Mutex);
            const bool exact = state->Generation == aAttempt.m_ownerGeneration &&
                state->ActiveAttemptId == aAttempt.m_attemptId &&
                (state->Phase == AttemptPhase::Prepared ||
                 state->Phase == AttemptPhase::Committed);
            if (exact)
            {
                state->Phase = AttemptPhase::Abandoned;
                state->Poisoned = true;
                CloseAdmission(*state);
                state->Drained.notify_all();
            }
        }
        catch (...) { PoisonNoThrow(state); }
        aAttempt.Reset();
    }
    static void DropRegistration(const std::shared_ptr<State>& aState,
                                 uint64_t aGeneration) noexcept
    {
        if (!aState)
            return;
        try
        {
            std::unique_lock lock(aState->Mutex);
            if (!CurrentRegistration(*aState, aGeneration))
                return;
            CloseAdmission(*aState);
            aState->RegistrationOwned = false;
            const bool terminal = aState->ActiveCallbacks != 0u ||
                aState->Phase != AttemptPhase::None;
            if (terminal)
                aState->Poisoned = true;
            if (IsActiveOnThisThread(aState.get()))
            {
                aState->Poisoned = true;
                return;
            }
            aState->Drained.wait(lock, [&]() noexcept
            {
                return aState->ActiveCallbacks == 0u;
            });
            if (!terminal && aState->Phase == AttemptPhase::None)
                aState->Registered = false;
            aState->Drained.notify_all();
        }
        catch (...) { PoisonNoThrow(aState); }
    }
    static void CloseFacade(const std::shared_ptr<State>& aState) noexcept
    {
        if (!aState)
            return;
        try
        {
            std::unique_lock lock(aState->Mutex);
            aState->FacadeAlive = false;
            CloseAdmission(*aState);
            const bool terminal = aState->ActiveCallbacks != 0u ||
                aState->Phase != AttemptPhase::None;
            if (terminal)
                aState->Poisoned = true;
            if (IsActiveOnThisThread(aState.get()))
            {
                aState->Poisoned = true;
                return;
            }
            aState->Drained.wait(lock, [&]() noexcept
            {
                return aState->ActiveCallbacks == 0u;
            });
            if (!terminal && aState->Phase == AttemptPhase::None)
            {
                aState->Registered = false;
                aState->RegistrationOwned = false;
            }
            aState->Drained.notify_all();
        }
        catch (...) { PoisonNoThrow(aState); }
    }

    inline static thread_local ActiveCallbackLease* s_activeFrame{};
    std::shared_ptr<State> m_state;
};
