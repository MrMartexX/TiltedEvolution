# Equal-party quest PoC validation

The PoC remains diagnostic-only. Canonical repair state is not applied back into Skyrim quests or save files.

## Automated validation

`TPTests` exercises the game-independent canonical state, persistence, protocol, repair, reconnect, divergence, admission, quarantine, runtime-safety, compatibility authorization and crash-safe runtime-apply control plane.

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
- exact manifest-backed adapter authorization using record/override/script/adapter fingerprints;
- transactional runtime-apply sequencing with deferred world targets, checkpoint gate, Papyrus-quiescence gate and stable digest verification;
- campaign-bound runtime-apply idempotency/recovery sidecar with checksum, atomic replacement and backup fallback;
- crash recovery that blocks all new mutation after a possibly executed repair until the checkpoint is restored;
- persistence-before-publish barriers that make the recovery marker durable before a future Skyrim mutation may be dispatched;
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
- `DLC1ScrollHandlingChangeLoc` — `GameId(2, 0x00012F92)` for the validated vanilla/DLC network mapping.

`gameplay-type` remains provisional. Controller-like quests such as `CWResetGarrison1` are intentionally **not** declared runtime-safe merely because their Skyrim quest type is non-zero/non-miscellaneous.

## Runtime-safety dry-run layer

Admission and runtime mutation authority are separate decisions. Every admitted snapshot can be structurally evaluated into one of:

- `Blocked` — admission itself forbids shared runtime handling;
- `StageOnly` — a simple running quest with no alias/world topology that could eventually be considered for a generic stage transition;
- `Deferred` — the snapshot has resolved reference aliases or an active scene participant, so a future executor must wait for relevant world/session state;
- `RequiresAdapter` — generic repair is not considered safe;
- `RuntimeSafe` — available only through an exact compatibility-authorized native adapter token. No live quest currently receives such a token.

The default policy fails closed for risky state. `RequiresAdapter` is selected for inactive rollback targets, stopped/completed/failed targets, created references, location aliases, quest-object aliases, unresolved reference aliases, and controller-like alias topology at or above 16 reference aliases.

A `StageOnly` result is **not** permission to call `SetStage`. The generated `PartyQuestApplyPlan` remains `DryRunOnly=true`. It records requirements such as a stage transition, objective verification, world-target waiting, Papyrus quiescence, and post-apply resnapshot verification, but there is no executor connected to Skyrim.

Live `QuestSnapshot[...]` diagnostics include `runtimeSafety`, `runtimeReason`, `applyActions`, and `dryRunOnly` so a later combined live test can show how real vanilla/modded quests fall into the safety buckets without mutating them.

## Exact runtime compatibility authorization

A public boolean can no longer turn an arbitrary quest into `RuntimeSafe`. The verified safety profile constructor is private and is issued only by `PartyQuestRuntimeCompatibilityPolicy` after exact requirement/fact matching.

A quest-specific adapter requirement currently binds:

- stable `QuestId`;
- compatibility profile version;
- resolved-record fingerprint;
- winning-override fingerprint;
- script fingerprint;
- native-adapter fingerprint.

Unknown quests and zero/missing evidence fail closed. Mismatches are reported separately as profile-version, resolved-record, winning-override, script or native-adapter mismatch. The manifest also rejects duplicate quest requirements instead of silently replacing a reviewed contract.

This is still a game-independent authorization model. Collection and distribution of real mod/script/DLL fingerprints belongs to the later campaign compatibility-manifest milestone.

## Transactional runtime-apply control plane

The new runtime-apply coordinator is intentionally disconnected from `TESQuest`, Papyrus and save APIs. It models the sequencing required before any live executor is allowed to exist.

Only `RuntimeSafe` plans are accepted. `StageOnly`, `Deferred` and `RequiresAdapter` plans cannot enter the critical apply lifecycle merely because they contain a stage action.

The modeled sequence is:

1. validate transaction and runtime authorization;
2. if world/cell targets are unavailable, hold the transaction in `DeferredWorld` without holding a save guard;
3. once targets are ready, enter the critical section and activate the save guard;
4. require a pre-repair checkpoint;
5. durably arm `RuntimeMutationMayHaveOccurred` **before** a future caller is permitted to execute any Skyrim/Papyrus mutation;
6. wait for Papyrus/event-queue quiescence;
7. resnapshot and compare against the canonical digest;
8. require two consecutive canonical digest samples before commit;
9. durably journal the committed transaction before the in-memory critical section is released.

A divergent resnapshot resets stability and prevents commit. Only one critical runtime transaction can be active at a time. Duplicate pending/committed transaction IDs are idempotent; reusing an ID for different canonical content is a transaction conflict.

The durability wrapper applies every state transition to a copy first. The candidate recovery state is persisted, and only then is it published in memory. Persistence failure or a throwing storage callback leaves the previous safe state intact. In particular, a failure while arming mutation leaves `RuntimeMutationMayHaveOccurred=false`, so a future executor must not run.

## Crash recovery and runtime sidecar

`PartyQuestRuntimeApplyPersistence` stores a campaign-bound recovery sidecar containing:

- `CampaignId`;
- committed runtime transaction fingerprints;
- an optional in-progress critical-repair marker.

The sidecar has its own magic/version, deterministic binary encoding, checksum, atomic `.tmp` replacement and `.bak` recovery. Payload-length parsing is overflow-safe. A sidecar from another valid campaign returns `CampaignMismatch` and is never imported.

Recovery behavior is fail-closed:

- committed transaction fingerprints survive restart, preventing duplicate runtime side effects;
- `DeferredWorld` work may resume without falsely claiming that saving is still locked;
- pre-mutation work is discarded after restart so a fresh current-canonical plan can be generated;
- if mutation may already have occurred, all new runtime apply work is blocked until an external pre-repair/LastKnownGood checkpoint restoration is explicitly acknowledged;
- inconsistent recovery markers are rejected.

The sidecar is **not yet wired into the final per-player co-op save layout**. That integration must happen together with separate co-op saves/checkpoints so a player's runtime transaction journal is not confused with another player's local replica.

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

This commit triggers full validation for the combined runtime-safety, compatibility-authorization, transactional apply, durable barrier and crash-recovery sidecar milestone. Required checks are:

- Build Windows;
- Build Linux;
- Equal party PoC diagnostics;
- PoC Windows runtime build;
- `TPTests`.

The new tests cover structural safety buckets, exact compatibility matching and each mismatch class, inability to forge a verified adapter profile through normal callers, provisional-plan refusal, checkpoint/Papyrus/stable-digest sequencing, deferred-world behavior, duplicate transaction idempotency, campaign-bound recovery, sidecar corruption/backup recovery, crash-after-mutation rollback barriers, persistence-before-mutation, persistence failure rollback, and durable commit publication.

## Next live validation

The earlier admission-only check remains deferred. The next user-side validation should be one combined diagnostic session against the existing persisted campaign, after the remaining non-mutating client integration is ready. It should validate quarantine plus `runtimeSafety`/`runtimeReason` distributions and recovery-sidecar/checkpoint wiring without executing canonical mutations.

Canonical `SetStage`, alias restoration, inventory mutation and save-file mutation remain disabled. Before any executor is enabled, the client still needs real separate co-op save/checkpoint integration, a concrete save guard, deferred cell/world readiness hooks, an observable Papyrus-quiescence mechanism, and real compatibility fingerprint collection/manifest exchange.
