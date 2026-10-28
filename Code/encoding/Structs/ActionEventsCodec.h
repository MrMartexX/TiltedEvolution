#pragma once

#include <TiltedCore/Stl.hpp>
#include <Structs/ActionEvent.h>

namespace ActionEventsCodec
{
using ActionsByActor = TiltedPhoques::SortedMap<uint32_t, TiltedPhoques::Vector<ActionEvent>>;
constexpr size_t MaximumActors = 256;
constexpr size_t MaximumActionsPerActor = 256;
constexpr size_t MaximumActions = 1024;
constexpr size_t MaximumVariablesPerAction = 8192;
constexpr size_t MaximumVariables = 65536;
constexpr size_t MaximumStringBytes = 4096;
constexpr size_t MaximumTotalStringBytes = 65536;

// Wire-compatible with ActionEvent::GenerateDifferential from a default action.
// A failed decode never publishes a partial candidate.
[[nodiscard]] bool Decode(TiltedPhoques::Buffer::Reader& aReader, ActionsByActor& aActions) noexcept;
}
