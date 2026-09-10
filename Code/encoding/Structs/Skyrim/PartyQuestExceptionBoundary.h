#pragma once

#include <utility>

/**
 * Small C++ exception boundary for callbacks that are required to remain
 * noexcept at Skyrim/native ABI edges.
 *
 * This catches C++ exceptions only. It deliberately does not claim to recover
 * from access violations or other Win32 SEH faults.
 */
class PartyQuestExceptionBoundary final
{
public:
    template <class TCallable>
    [[nodiscard]] static bool Invoke(TCallable&& aCallable) noexcept
    {
        try
        {
            std::forward<TCallable>(aCallable)();
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    template <class TResult, class TCallable>
    [[nodiscard]] static TResult InvokeOr(
        TResult aFallback,
        TCallable&& aCallable) noexcept
    {
        try
        {
            return std::forward<TCallable>(aCallable)();
        }
        catch (...)
        {
            return std::move(aFallback);
        }
    }
};
