# Equal-party quest PoC validation

The canonical equal-party repair path remains non-mutating inside Skyrim. The branch now also contains a game-independent, explicitly invoked filesystem stack for isolated co-op save replicas and immutable revision checkpoints; that stack is not connected to Skyrim save/load entry points yet.

## Automated validation

`TPTests` exercises the game-independent canonical state, persistence, protocol, repair, reconnect, divergence, admission, quarantine, runtime-safety, compatibility authorization, crash-safe runtime-apply control plane, per-player replica identity, filesystem copy verification and checkpoint/restore planning.

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
- server-side admission reclassification and logical quarantine migration;
- structural runtime-safety classification and non-executing apply plans;
- exact manifest-backed adapter authorization using record/override/script/adapter fingerprints;
- transactional runtime-apply sequencing with deferred world targets, checkpoint gate, Papyrus-quiescence gate and stable digest verification;
- persistence-before-mutation barriers and crash recovery that blocks new apply work until checkpoint restoration;
- stable per-character `PlayerProfileId` plus player-scoped runtime-apply recovery journal;
- conservative primary/`.tmp`/`.bak` recovery for side-effect journals;
- isolated co-op replica paths that never alias different campaigns or player profiles;
- verified create-only import of `.ess`, `.skse` and explicit external sidecars;
- source revalidation, destination confinement, temporary staging and post-copy digest verification;
- durable replica/checkpoint completion manifests bound to campaign and player identity;
- adoption of an exact complete orphaned file set after a crash before manifest publication;
- refusal to adopt partial/conflicting orphaned copies;
- snapshot-manager validation of imported replicas and checkpoints;
- immutable revision-scoped checkpoint publication and independent validation of multiple revisions;
- revision-bound checkpoint manifests and restore source selection;
- non-executing checkpoint restore plans that can target only the current co-op replica's save/external-sidecar paths;
- restore refusal when checkpoint bytes or stable identity no longer verify;
- transaction-scoped save-guard policy;
- deferred-world latest-revision queueing;
- Papyrus/event-generation quiescence tracking;
- automatic one-PC shadow-peer scenario for missed-update and digest-divergence repair.

## Enforced quest admission

The collector classifies observed quests as:

- `shared-candidate` — ordinary gameplay quest types, plus user-facing `None`/`Miscellaneous` quests;
- `service-candidate` — hidden `None`/`Miscellaneous` controller/tracker/helper candidates;
- `local-only` — currently used for quests without stages.

The server-side admission layer converts those observations into:

- `SharedProvisional` — admitted to diagnostic canonical campaign state, but not authorized for runtime mutation;
- `BlockedServiceCandidate`;
- `BlockedLocalOnly`;
- `BlockedConfirmedServiceQuest`.

Raw runtime facts travel with protocol-v3 transaction requests and are reclassified by the server. Known service identities override client-supplied facts.

The first evidence-backed service identities remain:

- `WIGreeting` — `GameId(0, 0x000C7919)`;
- `CRHoldExpansion` — `GameId(0, 0x000F9075)`;
- `DLC1ScrollHandlingChangeLoc` — `GameId(2, 0x00012F92)` for the validated vanilla/DLC network mapping.

`gameplay-type` remains provisional. Controller-like quests are not declared runtime-safe merely because their Skyrim quest type is non-zero.

## Runtime-safety and compatibility boundary

Admission and runtime mutation authority are separate decisions. An admitted snapshot is classified as `Blocked`, `StageOnly`, `Deferred`, `RequiresAdapter`, or `RuntimeSafe`.

`RuntimeSafe` is available only through an exact compatibility-authorized native-adapter token. A quest-specific requirement binds stable `QuestId`, compatibility profile version, resolved-record fingerprint, winning-override fingerprint, script fingerprint and native-adapter fingerprint. Unknown or incomplete evidence fails closed.

`StageOnly` is not permission to invoke `SetStage`. `PartyQuestApplyPlan::DryRunOnly` remains true and no live canonical executor is connected to Skyrim.

## Transactional runtime apply and crash recovery

The runtime-apply coordinator models:

1. runtime authorization;
2. deferred world readiness;
3. critical save-guard entry;
4. pre-repair checkpoint requirement;
5. durable `RuntimeMutationMayHaveOccurred` arm;
6. Papyrus/event-queue quiescence;
7. canonical resnapshot verification;
8. two stable digest samples;
9. durable transaction commit.

A divergent resnapshot resets stability and prevents commit. One critical runtime transaction may be active at a time. Duplicate transaction IDs are idempotent only for the same fingerprint.

The runtime recovery sidecar is scoped to both `CampaignId` and stable `PlayerProfileId`. Committed transaction fingerprints survive restart. If mutation may already have occurred, new runtime work remains blocked until external checkpoint restoration is acknowledged.

A valid fully written `.tmp` can recover an interrupted sidecar replacement. An older `.bak` is not silently treated as current side-effect truth and instead produces `BackupRecoveryRequired`.

## Co-op replica filesystem milestone

The executable library-level tree is:

```text
CoopCampaigns/
  Campaign_<CampaignId>/
    Player_<PlayerProfileId>/
      checkpoints/
        PreRepair/
          Revision_<WorldRevision>/
            manifest.bin
            saves/
            sidecars/external/
        LastKnownGood/
          Revision_<WorldRevision>/
            ...
      saves/
      sidecars/
        party_quest_runtime_apply.bin
        external/
      metadata/
        replica_manifest.bin
```

`PartyQuestReplicaFileExecutor` is deliberately create-only. It does not overwrite an existing replica/checkpoint. Before publication it re-reads source bytes, checks expected size/digest, rejects source/destination scope violations, stages every output to a temporary sibling and verifies the complete staged set. Final paths are published only afterward. Normal in-process publication errors roll back outputs created by that call.

`PartyQuestReplicaManifestStore` is the durable completion marker. Copied files without a valid identity-bound manifest are not sufficient proof that a multi-file snapshot completed. Future validation reloads the manifest and re-verifies final bytes.

`PartyQuestReplicaSnapshotManager` combines copy, verification and manifest durability. It can safely finish a crash window where every exact file was already published but the process died before manifest creation. Partial or conflicting orphaned output is rejected.

Production-oriented checkpoint calls use `BuildRevisionCheckpointPlan` → `ExecuteRevisionCheckpoint` → `BuildRevisionCheckpointManifest`/`EnsureRevisionCheckpoint`. A non-zero `CampaignWorldRevision` selects an immutable `Revision_<hex>` subtree. Two checkpoint revisions therefore coexist rather than replacing each other, and each is verified against its own manifest and bytes.

Legacy kind-root checkpoint methods remain available only for earlier tooling/tests; new recovery work should use `RevisionCheckpoint`.

## Checkpoint restore planning

`PartyQuestReplicaRestorePlanner` accepts legacy or revision checkpoints only after manifest and bytes verify for the expected campaign/player. For a `RevisionCheckpoint`, the source root is derived from its `CheckpointKind + CampaignWorldRevision`, preventing a restore plan from silently selecting another revision.

It maps checkpoint content back only into the current co-op replica's `saves/` and `sidecars/external/` paths. It cannot target solo saves, checkpoint storage, metadata, or `party_quest_runtime_apply.bin`.

Restore remains **planning-only**. No destructive replacement executor exists yet. That executor must have its own durable restore journal/rollback protocol before it is allowed to replace live replica files.

## Live validation already completed

The earlier one-PC shadow-peer session from persisted `WorldRevision=269` confirmed stable campaign identity, missed-update repair, same-revision digest-divergence repair, verified ACK convergence and continued accepted canonical progression. That session also supplied the first service-quest quarantine evidence.

## Current CI validation

This branch head triggers the full validation set for immutable revision checkpoint publication:

- Build Windows;
- Build Linux;
- Equal party PoC diagnostics;
- PoC Windows runtime build;
- `TPTests`.

The newest tests verify that revision checkpoints publish under distinct paths, preserve older revisions, round-trip their manifests, restore from the exact recorded revision, reject revision zero, and isolate corruption of one checkpoint revision from another.

## Next live validation

No user-side run is required for the filesystem types alone because they are not connected to Skyrim yet. The next useful live test remains one combined diagnostic session after client wiring can observe the new guardrails without enabling canonical mutation.

Before the first canonical Skyrim mutation, remaining blockers include:

- retention/selection policy for immutable checkpoint revisions;
- crash-resumable destructive checkpoint restore;
- real Skyrim save interception using `PartyQuestSaveGuard`;
- actual co-op replica/checkpoint routing in the client;
- real unloaded-cell/reference readiness callbacks;
- observable Papyrus/event-queue integration;
- real compatibility fingerprint collection/campaign manifest exchange.

Canonical `SetStage`, alias restoration, inventory mutation and canonical save mutation remain disabled.