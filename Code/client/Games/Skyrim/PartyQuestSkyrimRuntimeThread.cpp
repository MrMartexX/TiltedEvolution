#include <TiltedOnlinePCH.h>

#include <PartyQuestSkyrimRuntimeThread.h>
#include <PartyQuestP0LiveDiagnostics.h>

#include <atomic>

namespace
{
std::atomic<uint32_t> s_runtimeUpdateThreadId{0};
}

bool PartyQuestSkyrimRuntimeThread::ObserveCurrentUpdateThread() noexcept
{
    const uint32_t currentThreadId = GetCurrentThreadId();
    if (currentThreadId == 0)
        return false;

    uint32_t expected = 0;
    if (s_runtimeUpdateThreadId.compare_exchange_strong(
            expected,
            currentThreadId,
            std::memory_order_acq_rel,
            std::memory_order_acquire))
    {
        PartyQuestP0LiveDiagnostics::RecordRuntimeThreadObservation(
            true, currentThreadId, currentThreadId);
        return true;
    }

    const bool accepted = expected == currentThreadId;
    if (!accepted)
    {
        PartyQuestP0LiveDiagnostics::RecordRuntimeThreadObservation(
            false, expected, currentThreadId);
    }

    return accepted;
}

bool PartyQuestSkyrimRuntimeThread::IsCurrentUpdateThread() noexcept
{
    const uint32_t boundThreadId =
        s_runtimeUpdateThreadId.load(std::memory_order_acquire);
    return boundThreadId != 0 && boundThreadId == GetCurrentThreadId();
}

uint32_t PartyQuestSkyrimRuntimeThread::GetBoundThreadIdForDiagnostics() noexcept
{
    return s_runtimeUpdateThreadId.load(std::memory_order_acquire);
}
