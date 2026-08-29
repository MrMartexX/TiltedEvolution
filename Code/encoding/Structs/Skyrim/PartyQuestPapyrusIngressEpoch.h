#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

/**
 * Poll-independent generation fence for concrete Papyrus work-ingress hooks.
 *
 * BeginIngress() publishes an active producer before the generation edge. The
 * matching scope publishes a second generation edge before removing that active
 * producer. A read-only VM sampler accepts a snapshot only when stamps captured
 * around the snapshot have the same non-zero generation and both observe zero
 * active producers. Work that is queued after the first stamp, or finishes while
 * the VM locks are held, therefore invalidates the sample rather than producing a
 * false Idle observation.
 *
 * The counter is deliberately independent of observer polling and queue counts.
 * Overflow or an unbalanced scope permanently poisons the process-local source;
 * callers must then fail closed for the rest of the process lifetime.
 */
class PartyQuestPapyrusIngressEpoch final
{
public:
    struct Stamp final
    {
        uint64_t Generation{};
        uint32_t ActiveIngress{};
        bool Healthy{};
    };

    class Scope final
    {
    public:
        Scope() noexcept = default;
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

        Scope(Scope&& aOther) noexcept
            : m_pOwner(aOther.m_pOwner)
        {
            aOther.m_pOwner = nullptr;
        }

        Scope& operator=(Scope&& aOther) noexcept
        {
            if (this != &aOther)
            {
                Release();
                m_pOwner = aOther.m_pOwner;
                aOther.m_pOwner = nullptr;
            }
            return *this;
        }

        ~Scope() noexcept
        {
            Release();
        }

        [[nodiscard]] bool IsActive() const noexcept
        {
            return m_pOwner != nullptr;
        }

        void Release() noexcept
        {
            if (!m_pOwner)
                return;
            m_pOwner->EndIngress();
            m_pOwner = nullptr;
        }

    private:
        friend class PartyQuestPapyrusIngressEpoch;

        explicit Scope(PartyQuestPapyrusIngressEpoch& aOwner) noexcept
            : m_pOwner(&aOwner)
        {
        }

        PartyQuestPapyrusIngressEpoch* m_pOwner{};
    };

    PartyQuestPapyrusIngressEpoch() noexcept = default;
    PartyQuestPapyrusIngressEpoch(const PartyQuestPapyrusIngressEpoch&) = delete;
    PartyQuestPapyrusIngressEpoch& operator=(
        const PartyQuestPapyrusIngressEpoch&) = delete;

    [[nodiscard]] Scope BeginIngress() noexcept
    {
        if (!m_healthy.load(std::memory_order_acquire))
            return {};

        const uint32_t priorActive =
            m_activeIngress.fetch_add(1, std::memory_order_acq_rel);
        if (priorActive == std::numeric_limits<uint32_t>::max())
        {
            m_healthy.store(false, std::memory_order_release);
            return {};
        }

        if (!AdvanceGeneration())
        {
            m_activeIngress.fetch_sub(1, std::memory_order_acq_rel);
            return {};
        }

        m_ingressCount.fetch_add(1, std::memory_order_relaxed);
        return Scope(*this);
    }

    [[nodiscard]] Stamp Capture() const noexcept
    {
        Stamp result;
        result.Generation = m_generation.load(std::memory_order_acquire);
        result.ActiveIngress = m_activeIngress.load(std::memory_order_acquire);
        result.Healthy = m_healthy.load(std::memory_order_acquire);
        return result;
    }

    [[nodiscard]] static bool IsStable(
        const Stamp& acBefore,
        const Stamp& acAfter) noexcept
    {
        return acBefore.Healthy && acAfter.Healthy &&
            acBefore.Generation != 0 &&
            acBefore.Generation == acAfter.Generation &&
            acBefore.ActiveIngress == 0 &&
            acAfter.ActiveIngress == 0;
    }

    [[nodiscard]] uint64_t GetIngressCount() const noexcept
    {
        return m_ingressCount.load(std::memory_order_acquire);
    }

private:
    [[nodiscard]] bool AdvanceGeneration() noexcept
    {
        const uint64_t prior =
            m_generation.fetch_add(1, std::memory_order_acq_rel);
        if (prior == std::numeric_limits<uint64_t>::max())
        {
            m_healthy.store(false, std::memory_order_release);
            return false;
        }
        return true;
    }

    void EndIngress() noexcept
    {
        (void)AdvanceGeneration();
        const uint32_t prior =
            m_activeIngress.fetch_sub(1, std::memory_order_acq_rel);
        if (prior == 0)
        {
            m_activeIngress.store(0, std::memory_order_release);
            m_healthy.store(false, std::memory_order_release);
        }
    }

    std::atomic<uint64_t> m_generation{1};
    std::atomic<uint64_t> m_ingressCount{0};
    std::atomic<uint32_t> m_activeIngress{0};
    std::atomic_bool m_healthy{true};
};
