#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeApply.h>

struct ModSystem;

/**
 * Narrow Skyrim-native executor for the first equal-party canonical mutation.
 *
 * This class is intentionally not a dispatcher and owns no authority. It may be
 * called only synchronously from PartyQuestRuntimeMutationDispatchGate after the
 * durable mutation barrier, exact compatibility revalidation and process
 * generation execution lease are already held.
 *
 * The executor freshly resolves GameId -> local FormID -> TESQuest at point of
 * use, maps the form back to the same GameId, accepts only a running stage-only
 * snapshot with no alias/world/created-reference mutation surface, and calls the
 * existing native TESQuest::SetStage implementation. It never starts/stops a
 * quest, touches aliases, inventories or world references.
 *
 * Merely compiling this executor does not enable canonical mutation. The current
 * production runtime-safety/profile pipeline remains dry-run/fail-closed until
 * its live evidence prerequisites are satisfied.
 */
class PartyQuestSkyrimStageMutationExecutor final
{
public:
    [[nodiscard]] static bool Execute(
        const PartyQuestRuntimeApplyRequest& acRequest,
        ModSystem& aModSystem) noexcept;
};
