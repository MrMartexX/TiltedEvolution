#include <Structs/ActionEventsCodec.h>
#include <StringCache.h>

#include <array>
#include <limits>
#include <utility>

static_assert(noexcept(std::declval<ActionEventsCodec::ActionsByActor&>().swap(
    std::declval<ActionEventsCodec::ActionsByActor&>())));

namespace
{
using TiltedPhoques::Buffer;
using TiltedPhoques::String;

bool ReadVarInt(Buffer::Reader& aReader, uint64_t& aValue) noexcept
{
    aValue = 0;
    for (uint32_t shift = 0; shift < 64; shift += 7)
    {
        uint64_t part = 0;
        uint64_t more = 0;
        if (!aReader.ReadBits(part, 7) || !aReader.ReadBits(more, 1) ||
            (shift == 63 && (part > 1 || more != 0)))
            return false;
        aValue |= part << shift;
        if (more == 0)
            return true;
    }
    return false;
}

bool Read32(Buffer::Reader& aReader, uint32_t& aValue) noexcept
{
    uint64_t value = 0;
    if (!ReadVarInt(aReader, value) || value > std::numeric_limits<uint32_t>::max())
        return false;
    aValue = static_cast<uint32_t>(value);
    return true;
}

bool ReadString(Buffer::Reader& aReader, String& aValue, size_t& aStringBudget)
{
    uint64_t size = 0;
    if (!ReadVarInt(aReader, size) || size > ActionEventsCodec::MaximumStringBytes || size > aStringBudget)
        return false;
    aStringBudget -= static_cast<size_t>(size);
    aValue.resize(static_cast<size_t>(size));
    return aReader.ReadBytes(reinterpret_cast<uint8_t*>(aValue.data()), aValue.size());
}

bool ReadCachedString(Buffer::Reader& aReader, CachedString& aValue, size_t& aStringBudget)
{
    uint64_t cached = 0;
    if (!aReader.ReadBits(cached, 1))
        return false;
    if (cached == 0)
        return ReadString(aReader, static_cast<String&>(aValue), aStringBudget);

    uint32_t id = 0;
    if (!Read32(aReader, id))
        return false;
    const auto value = StringCache::Get()[id];
    if (!value || value->size() > ActionEventsCodec::MaximumStringBytes || value->size() > aStringBudget)
        return false;
    aStringBudget -= value->size();
    static_cast<String&>(aValue) = *value;
    return true;
}

bool ReadVariables(Buffer::Reader& aReader, AnimationVariables& aVariables, size_t& aVariableBudget)
{
    uint64_t booleans = 0;
    uint64_t integers = 0;
    uint64_t floats = 0;
    if (!ReadVarInt(aReader, booleans) || !ReadVarInt(aReader, integers) || !ReadVarInt(aReader, floats))
        return false;
    const uint64_t maximum = ActionEventsCodec::MaximumVariablesPerAction;
    if (booleans > maximum || integers > maximum - booleans || floats > maximum - booleans - integers)
        return false;
    const size_t count = static_cast<size_t>(booleans + integers + floats);
    if (count > aVariableBudget)
        return false;
    aVariableBudget -= count;

    uint64_t byteCount = 0;
    const size_t expected = (count + 7) / 8;
    if (!ReadVarInt(aReader, byteCount) || byteCount != expected)
        return false;
    std::array<uint8_t, (ActionEventsCodec::MaximumVariablesPerAction + 7) / 8> changed{};
    if (!aReader.ReadBytes(changed.data(), expected))
        return false;
    const auto bit = [&](size_t aIndex) { return (changed[aIndex / 8] & (1u << (aIndex % 8))) != 0; };

    aVariables.Booleans.resize(static_cast<size_t>(booleans));
    aVariables.Integers.resize(static_cast<size_t>(integers));
    aVariables.Floats.resize(static_cast<size_t>(floats));
    size_t index = 0;
    for (size_t i = 0; i < booleans; ++i)
        aVariables.Booleans[i] = bit(index++);
    for (auto& integer : aVariables.Integers)
        if (bit(index++) && !Read32(aReader, integer))
            return false;
    for (auto& value : aVariables.Floats)
        if (bit(index++) && !aReader.ReadBytes(reinterpret_cast<uint8_t*>(&value), sizeof(value)))
            return false;
    return true;
}

bool ReadAction(Buffer::Reader& aReader, ActionEvent& aAction, size_t& aVariables, size_t& aStrings)
{
    uint64_t flags = 0;
    if (!aReader.ReadBits(flags, 8) || !ReadVarInt(aReader, aAction.Tick))
        return false;
    if ((flags & 1) && !Read32(aReader, aAction.ActionId))
        return false;
    if ((flags & 2) && !Read32(aReader, aAction.TargetId))
        return false;
    if ((flags & 4) && !Read32(aReader, aAction.IdleId))
        return false;
    if (flags & 8)
    {
        uint64_t first = 0;
        uint64_t second = 0;
        if (!aReader.ReadBits(first, 32) || !aReader.ReadBits(second, 32))
            return false;
        aAction.State1 = static_cast<uint32_t>(first);
        aAction.State2 = static_cast<uint32_t>(second);
    }
    if ((flags & 16) && !Read32(aReader, aAction.Type))
        return false;
    if ((flags & 32) && !ReadCachedString(aReader, aAction.EventName, aStrings))
        return false;
    if ((flags & 64) && !ReadCachedString(aReader, aAction.TargetEventName, aStrings))
        return false;
    return !(flags & 128) || ReadVariables(aReader, aAction.Variables, aVariables);
}
}

bool ActionEventsCodec::Decode(TiltedPhoques::Buffer::Reader& aReader, ActionsByActor& aActions) noexcept
{
    try
    {
        uint64_t count = 0;
        if (!ReadVarInt(aReader, count) || count > MaximumActors)
            return false;
        ActionsByActor candidate;
        size_t actionsLeft = MaximumActions;
        size_t variablesLeft = MaximumVariables;
        size_t stringsLeft = MaximumTotalStringBytes;
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t id = 0;
            uint64_t actionCount = 0;
            if (!Read32(aReader, id) || !ReadVarInt(aReader, actionCount) ||
                actionCount > MaximumActionsPerActor || actionCount > actionsLeft)
                return false;
            actionsLeft -= static_cast<size_t>(actionCount);
            auto [it, inserted] = candidate.emplace(id, TiltedPhoques::Vector<ActionEvent>{});
            if (!inserted)
                return false;
            auto& actions = it->second;
            actions.resize(static_cast<size_t>(actionCount));
            for (auto& action : actions)
                if (!ReadAction(aReader, action, variablesLeft, stringsLeft))
                    return false;
        }
        // Only accepted messages may update the process string-cache demand.
        for (const auto& [id, actions] : candidate)
            for (const auto& action : actions)
                for (const auto* value : {&action.EventName, &action.TargetEventName})
                    if (!value->empty() && !StringCache::Get().Contains(*value))
                        StringCache::Get().AddWanted(*value);
        aActions.swap(candidate);
        return true;
    }
    catch (...)
    {
        // No candidate or authority is published after allocation/validation failure.
        return false;
    }
}
