# Task 02 — Wire production runtime bootstrap

Read `../MASTER-HANDOFF.md`. Start only after Task 01 is accepted into the current integration HEAD. Follow the master branch/CI/reporting protocol and work only on this task.

## Goal

Bind the process `PartyQuestRuntimeSessionOwner` in the real client from a verified campaign plus current Skyrim character lineage, then release/invalidate it at the already-covered lifecycle boundaries. Do not infer identity from a filename or ordinary local setting.

## Starting points to verify

- `PartyQuestRuntimeSessionBootstrap::BindProcessOwner()` currently exists but production callsites are incomplete.
- `PartyQuestSkyrimPlayerProfileLineageResolver` reads two stable snapshots from the already-loaded `SkyrimTogetherLineageBridge.dll` and pins its module.
- Bridge implementation exists under `Code/skse_lineage_bridge`.
- `QuestService` owns client protocol/campaign state; `PartyService`, `TransportService`, `SaveLoad` and `TiltedOnlineApp` already invalidate/release parts of the runtime owner.

## Required work

1. Trace authoritative sources for campaign ID, server session/connection generation, local player ID and character lineage. Document which object owns each value.
2. Select one production bootstrap point where all evidence is simultaneously valid. Bind only after protocol/campaign admission and verified current-character lineage, under the existing generation lease.
3. Build the replica root through `PartyQuestCoopSaveLayout`; require an absolute controlled path and exact campaign/profile match.
4. Treat the bridge ABI as untrusted input: validate ABI version, structure size, provider identity, runtime identity, generation, stable double-snapshot, string bounds and semantic validity. Do not `LoadLibrary` an arbitrary search-path module.
5. Make duplicate same-identity bind idempotent. Reject conflicting campaign/profile/generation bind without replacing the published owner.
6. Define retry behavior when campaign arrives before lineage or lineage before campaign. Retry only on a new authoritative signal; no hot polling on the game thread.
7. Ensure disconnect, party leave, campaign switch, LoadGame, NewGame, MainMenu and shutdown revoke the bound session through existing primitives. Reconnect/reload must require fresh evidence and a new valid bind.
8. Diagnostics must distinguish missing bridge, unsupported ABI/runtime, unstable snapshot, incomplete lifecycle coverage, generation mismatch, invalid layout and owner conflict.

## Regression tests

Cover successful bind; missing bridge/provider export; wrong ABI/size/runtime/provider; unstable two-snapshot read; stale generation; invalid campaign; invalid/relative root; duplicate identical bind; conflicting bind; campaign-before-lineage; lineage-before-campaign; disconnect/reconnect; campaign ABA with same quest IDs; LoadGame/NewGame/MainMenu rebinding; exception/allocation failure; concurrent invalidation during bind.

Prove publication is atomic: any rejected attempt leaves either the prior valid owner unchanged or no owner, never a partially hydrated session.

## Done when

- A real accepted client session reaches `BindProcessOwner()` with verified immutable evidence.
- There is exactly one process owner and one lifecycle universe.
- Every rebinding requires fresh generation/campaign/lineage authorization.
- Failure is deterministic and fail closed; no main-thread repeated scan/poll loop is introduced.
- Canonical mutation remains disabled and all required CI is FULL GREEN with exact counts.
