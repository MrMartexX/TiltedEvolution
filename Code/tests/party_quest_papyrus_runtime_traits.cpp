#include <Structs/Skyrim/PartyQuestPapyrusQuiescence.h>
#include <Structs/Skyrim/PartyQuestPapyrusRuntimeMonitor.h>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<PartyQuestPapyrusQuiescenceTracker>);
static_assert(!std::is_copy_assignable_v<PartyQuestPapyrusQuiescenceTracker>);
static_assert(!std::is_move_constructible_v<PartyQuestPapyrusQuiescenceTracker>);
static_assert(!std::is_move_assignable_v<PartyQuestPapyrusQuiescenceTracker>);

static_assert(!std::is_copy_constructible_v<PartyQuestPapyrusRuntimeMonitor>);
static_assert(!std::is_copy_assignable_v<PartyQuestPapyrusRuntimeMonitor>);
static_assert(!std::is_move_constructible_v<PartyQuestPapyrusRuntimeMonitor>);
static_assert(!std::is_move_assignable_v<PartyQuestPapyrusRuntimeMonitor>);
