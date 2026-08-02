# Equal-party quest PoC validation

The PoC remains diagnostic-only. Canonical repair state is not applied back into Skyrim quests or save files.

## Automated validation

`TPTests` exercises the game-independent canonical state, persistence, protocol, repair, reconnect, divergence, admission, quarantine and runtime-safety planning layers.

Current coverage includes:

- deterministic `QuestSnapshot` canonicalization and digests;
- world/per-quest revision assignment;
- idempotent transaction IDs and conflicting transaction detection;
- checkpoint/journal persistence, replay validation, checksums, atomic replacement and `.bak` recovery;
- durable save-before-publish behavior and failed-write rollback;
- stable persisted `CampaignId` independent of transient Party IDs;
- replica reports, deterministic repair plans and repair ACK verification;
- reconnect-safe client protocol ID allocation;
- retained protocol replica across transient `PlayerId` rebinding;
- reset on genuine `CampaignId` change;
- campaign verification gating and per-quest submission coalescing;
- two simulated connected clients converging after independent divergence;
- same-revision payload divergence detected by digest;
- missed canonical update detected by quest revision;
- client-only quest state preserved locally and never admitted into canonical campaign state by repair;
- repair ACK correlation isolated per authenticated client session;
- repair divergence summaries split into missing, revision mismatch, digest mismatch, client-only and quarantined-removal counts;
- server-side admission reclassification from raw runtime facts;
- explicit admission rejection without retry/repair storms;
- logical quarantine migration for confirmed legacy service quests while preserving journal/world revision history;
- structural runtime-safety classification and non-executing apply plans;
- automatic one-PC shadow-peer scenario for missed-update and digest-divergence repair.

## Enforced quest admission

The collector classifies observed quests as:

- `shared-candidate` — ordinary gameplay quest types, plus user-facing `None`/`Miscellaneous` quests;
- `service-candidate` — hidden `None`/`Miscellaneous` controller/tracker/helper candidates;
- `local-only` — currently used for quests without stages.

The admission layer converts those observations into server-side policy results:

- `SharedProvisional` — admitted to the diagnostic canonical campaign, but **not** approved for future Skyrim runtime mutation;
- `BlockedServiceCandidate` — excluded from canonical transactions;
- `BlockedLocalOnly` — excluded from canonical transactions;
- `BlockedConfirmedServiceQuest` — an identity-level override for service quests confirmed by live evidence.

Raw `QuestType`, stage-presence, HUD-display and display-name facts travel with protocol-v3 transaction requests. The client suppresses obvious service/local-only observations early, but the server independently reclassifies requests. Known service identities override client-supplied facts.

The first confirmed-service identity set comes directly from the validated live session:

- `WIGreeting` — `GameId(0, 0x000C7919)`;
- `CRHoldExpansion` — `GameId(0, 0x000F9075)`;
- `DLC1ScrollHandlingChangeLoc` — `GameId(2, 0x00012F92)` for the validated vanilla/DLC network mod mapping.

`gameplay-type` remains provisional. Controller-like quests such as `CWResetGarrison1` are intentionally **not** declared runtime-safe merely because their Skyrim quest type is non-zero/non-miscellaneous.

## Runtime-safety dry-run layer

Admission and runtime mutation authority are now separate decisions. Every admitted snapshot can be structurally evaluated into one of:

- `Blocked` — admission itself forbids shared runtime handling;
- `StageOnly` — a simple running quest with no alias/world topology that could eventually be considered for a generic stage transition;
- `Deferred` — the snapshot has resolved reference aliases or an active scene participant, so a future executor must wait for relevant world/session state;
- `RequiresAdapter` — generic repair is not considered safe;
- `RuntimeSafe` — reserved for an explicit verified native adapter profile. No live quest currently receives this profile.

The default policy fails closed for risky state. `RequiresAdapter` is selected for inactive rollback targets, stopped/completed/failed targets, created references, location aliases, quest-object aliases, unresolved reference aliases, and controller-like alias topology at or above 16 reference aliases.

A `StageOnly` result is **not** permission to call `SetStage`. The generated `PartyQuestApplyPlan` is always `DryRunOnly=true` in this milestone. It records requirements such as a stage transition, objective verification, world-target waiting, Papyrus quiescence, and post-apply resnapshot verification, but there is no executor connected to Skyrim.

Live `QuestSnapshot[...]` diagnostics now include `runtimeSafety`, `runtimeReason`, `applyActions`, and `dryRunOnly` so the later combined live test can show how real vanilla/modded quests fall into the safety buckets without mutating them.

## Legacy campaign quarantine migration

Existing persisted campaigns may already contain service snapshots and journal events from earlier diagnostic builds. The migration deliberately does not rewrite that history because doing so would invalidate world revisions, replay verification, transaction-id idempotency and recovery semantics.

Instead:

1. historical checkpoint/journal data remains intact;
2. confirmed service quest snapshots are excluded from the shared repair surface;
3. fresh replicas never receive those service snapshots;
4. replicas that still contain a confirmed service quest receive an explicit `RemovedQuestIds` repair operation;
5. the removal does not roll back or renumber `WorldRevision`;
6. future observations of those quests cannot create new canonical transactions.

This is a logical quarantine migration, not destructive journal pruning.

## One-PC shadow peer live mode

For a user with one Skyrim installation and one PC, the server can create a synthetic second protocol replica without launching another Skyrim process.

Enable:

```ini
[Gameplay]
bEnablePartyQuestProtocolDiagnostics=true
bEnablePartyQuestStatePersistence=true
bEnablePartyQuestShadowPeerTest=true
sPartyQuestStatePath=state/party_quest_campaign.bin
```

The shadow peer is server-local and never mutates Skyrim. It uses synthetic client id `0xFFFFFFFE` only inside `PartyQuestProtocolCoordinator`.

## Live validation completed

The one-PC shadow-peer session starting from persisted `WorldRevision=269` confirmed:

- stable `CampaignId=A0C27D9E9A1E822D3EE502D620B25F94`;
- initial shadow convergence;
- baseline canonical update at revision 270;
- deliberate missed update at revision 271;
- missed-update repair with one revision mismatch;
- deliberate same-revision payload corruption;
- one `DigestMismatch` repair;
- verified repair ACK and final convergence;
- clean shadow-peer disconnect;
- continued real-client progression after the test with accepted canonical transactions.

The same live log produced the evidence used for the first service blocklist: 71 service-candidate observations were dominated by `DLC1ScrollHandlingChangeLoc`, `CRHoldExpansion`, and `WIGreeting`.

## Current CI validation

Admission/quarantine code was validated at commit `2aea537ec8e1636feb03bbee6069b55f0bea3eae`.

The runtime-safety dry-run milestone is validated by the workflow run triggered from the current branch head. Required checks are:

- Build Windows;
- Build Linux;
- Equal party PoC diagnostics;
- PoC Windows runtime build;
- `TPTests`, including the new runtime-safety planner cases.

The new runtime-safety tests cover admission blocking, simple stage-only planning, deferred resolved aliases, active-scene deferral, unresolved/quest-object/location/created-reference adapter requirements, complex controller-like alias topology, inactive rollback protection, terminal-state protection, and the rule that only an explicit verified native adapter profile may classify a quest as `RuntimeSafe`.

## Next live validation

The admission-only live check can remain deferred. Continue building the protection layers, then use one combined live session against the existing persisted campaign to validate quarantine plus the new `runtimeSafety`/`runtimeReason` diagnostics over real quests.

Canonical `SetStage`, alias restoration, inventory mutation and save-file mutation remain disabled. The next implementation milestone should add the transactional runtime-apply lifecycle around save guards, deferred world targets, Papyrus quiescence, stable resnapshot verification and idempotent commit tracking before any executor is allowed to touch Skyrim.
