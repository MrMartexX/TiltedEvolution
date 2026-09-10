#pragma once

#include <cstdint>

[[nodiscard]] constexpr bool IsPlausiblePartyQuestPapyrusHashMapHeader(
    uint32_t aCapacity,
    uint32_t aFree,
    uint32_t aFreeSearchStart) noexcept
{
    constexpr uint32_t kMaximumPlausibleCapacity = 1u << 24;

    if (aCapacity == 0)
        return aFree == 0 && aFreeSearchStart == 0;

    const bool capacityIsPowerOfTwo =
        (aCapacity & (aCapacity - 1)) == 0;
    return capacityIsPowerOfTwo &&
        aCapacity <= kMaximumPlausibleCapacity && aFree <= aCapacity &&
        aFreeSearchStart <= aCapacity;
}
