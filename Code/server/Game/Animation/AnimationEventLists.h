#pragma once

#include <string_view>
#include <TiltedCore/Stl.hpp>

using TiltedPhoques::Set;
using TiltedPhoques::Map;

namespace AnimationEventLists
{
extern const Map<std::string_view, std::string_view> kIdleToInstant;

extern const Set<std::string_view> kExitSpecial;

extern const Set<std::string_view> kIgnore;
} // namespace
