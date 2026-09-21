#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>

enum class PartyQuestPreLoadAdmission : uint8_t
{
    Blocked,
    Allowed
};

enum class PartyQuestPreLoadAdmissionRegistrationStatus : uint8_t
{
    Registered,
    InvalidCallback,
    OwnerAlreadyRegistered
};

enum class PartyQuestPreLoadAdmissionUnregisterStatus : uint8_t
{
    Unregistered,
    StaleRegistration,
    ReentrantCallbackDeferred,
    SynchronizationFailed
};

/**
 * Process-local fail-closed bridge between the Skyrim Load_Impl hook and its
 * runtime owner.
 *
 * Evaluate() never retains request arguments: the future LoadGame adapter may
 * ask only whether the current attempt is admitted. The callback is invoked
 * without this registry's mutex. Unregister() first closes admission and clears
 * the published callback, then waits for every previously acquired callback
 * lease before permitting the owner to destroy its context.
 */
class PartyQuestPreLoadAdmissionRegistry final
{
public:
    using Callback = PartyQuestPreLoadAdmission (*)(void*);

    class Registration final
    {
    public:
        Registration() noexcept = default;
        Registration(Registration&& aOther) noexcept
            : m_registry(aOther.m_registry)
            , m_generation(aOther.m_generation)
        {
            aOther.Reset();
        }

        // Replacing a live token would orphan its published registration.
        Registration& operator=(Registration&&) = delete;

        Registration(const Registration&) = delete;
        Registration& operator=(const Registration&) = delete;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_registry != nullptr && m_generation != 0u;
        }

    private:
        friend class PartyQuestPreLoadAdmissionRegistry;

        Registration(
            PartyQuestPreLoadAdmissionRegistry* apRegistry,
            uint64_t aGeneration) noexcept
            : m_registry(apRegistry)
            , m_generation(aGeneration)
        {
        }

        void Reset() noexcept
        {
            m_registry = nullptr;
            m_generation = 0u;
        }

        PartyQuestPreLoadAdmissionRegistry* m_registry{};
        uint64_t m_generation{};
    };

    struct RegisterResult final
    {
        PartyQuestPreLoadAdmissionRegistrationStatus Status{
            PartyQuestPreLoadAdmissionRegistrationStatus::InvalidCallback};
        std::optional<Registration> Token;
    };

    [[nodiscard]] RegisterResult Register(
        void* apContext,
        Callback apCallback) noexcept
    {
        RegisterResult result;
        if (!apCallback)
            return result;

        try
        {
            std::lock_guard lock(m_mutex);
            if (m_registered)
            {
                result.Status = PartyQuestPreLoadAdmissionRegistrationStatus::
                    OwnerAlreadyRegistered;
                return result;
            }

            uint64_t generation = ++m_generation;
            if (generation == 0u)
                generation = ++m_generation;
            m_context = apContext;
            m_callback = apCallback;
            m_registered = true;
            m_accepting = true;
            result.Token.emplace(Registration(this, generation));
            result.Status =
                PartyQuestPreLoadAdmissionRegistrationStatus::Registered;
            return result;
        }
        catch (...)
        {
            return result;
        }
    }

    [[nodiscard]] PartyQuestPreLoadAdmissionUnregisterStatus Unregister(
        Registration& aRegistration) noexcept
    {
        try
        {
            std::unique_lock lock(m_mutex);
            if (!aRegistration.IsValid() ||
                aRegistration.m_registry != this || !m_registered ||
                aRegistration.m_generation != m_generation)
            {
                return PartyQuestPreLoadAdmissionUnregisterStatus::
                    StaleRegistration;
            }

            // No newly arriving load attempt may enter the owner while its
            // previously leased callbacks are draining.
            m_accepting = false;
            m_callback = nullptr;
            m_context = nullptr;

            // Waiting for oneself is impossible. Keep the exact token live so
            // the owner can retry unregister after its callback returns.
            if (s_activeRegistry == this)
            {
                return PartyQuestPreLoadAdmissionUnregisterStatus::
                    ReentrantCallbackDeferred;
            }

            m_drained.wait(lock, [this]() noexcept
            {
                return m_activeCallbacks == 0u;
            });
            m_registered = false;
            aRegistration.Reset();
            return PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered;
        }
        catch (...)
        {
            return PartyQuestPreLoadAdmissionUnregisterStatus::
                SynchronizationFailed;
        }
    }

    [[nodiscard]] PartyQuestPreLoadAdmission Evaluate() noexcept
    {
        Callback callback = nullptr;
        void* pContext = nullptr;
        uint64_t generation = 0u;
        try
        {
            std::unique_lock lock(m_mutex);
            if (!m_registered || !m_accepting || !m_callback)
                return PartyQuestPreLoadAdmission::Blocked;

            callback = m_callback;
            pContext = m_context;
            generation = m_generation;
            ++m_activeCallbacks;
        }
        catch (...)
        {
            return PartyQuestPreLoadAdmission::Blocked;
        }

        ActiveCallbackLease lease(*this);
        try
        {
            if (callback(pContext) != PartyQuestPreLoadAdmission::Allowed)
                return PartyQuestPreLoadAdmission::Blocked;

            // A concurrently closing owner supersedes an earlier callback
            // decision while the callback lease is still active.
            std::lock_guard lock(m_mutex);
            return m_registered && m_accepting && m_callback == callback &&
                    m_context == pContext && m_generation == generation ?
                PartyQuestPreLoadAdmission::Allowed :
                PartyQuestPreLoadAdmission::Blocked;
        }
        catch (...)
        {
            return PartyQuestPreLoadAdmission::Blocked;
        }
    }

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
        explicit ActiveCallbackLease(
            PartyQuestPreLoadAdmissionRegistry& aRegistry) noexcept
            : m_registry(aRegistry)
            , m_previous(s_activeRegistry)
        {
            s_activeRegistry = &aRegistry;
        }

        ~ActiveCallbackLease() noexcept
        {
            s_activeRegistry = m_previous;
            try
            {
                std::lock_guard lock(m_registry.m_mutex);
                if (m_registry.m_activeCallbacks != 0u)
                    --m_registry.m_activeCallbacks;
                if (m_registry.m_activeCallbacks == 0u)
                    m_registry.m_drained.notify_all();
            }
            catch (...)
            {
                // Do not mutate registry state without its mutex. A failed
                // release deliberately leaves the active count nonzero, so a
                // concurrent unregister cannot falsely prove quiescence.
            }
        }

        ActiveCallbackLease(const ActiveCallbackLease&) = delete;
        ActiveCallbackLease& operator=(const ActiveCallbackLease&) = delete;

    private:
        PartyQuestPreLoadAdmissionRegistry& m_registry;
        PartyQuestPreLoadAdmissionRegistry* m_previous{};
    };

    inline static thread_local PartyQuestPreLoadAdmissionRegistry*
        s_activeRegistry{};

    std::mutex m_mutex;
    std::condition_variable m_drained;
    Callback m_callback{};
    void* m_context{};
    uint64_t m_generation{};
    uint64_t m_activeCallbacks{};
    bool m_registered{};
    bool m_accepting{};
};
