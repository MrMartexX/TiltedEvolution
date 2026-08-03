# Equal-party runtime apply guardrails

This document describes the non-executing safety/control-plane work that must be validated before the new server-authoritative quest protocol is allowed to mutate Skyrim runtime state.

## Current safety boundary

No code in this milestone executes canonical `TESQuest::ScriptSetStage`, restores aliases, mutates inventories, changes quest objects, writes Skyrim `.ess` saves, or replaces co-saves.

`PartyQuestApplyPlan::DryRunOnly` remains `true`.

## Admission is not mutation authority

A quest that enters `CampaignState` as `SharedProvisional` is not automatically safe to apply on another client.

Structural runtime safety is evaluated separately as:

- `Blocked`
- `StageOnly`
- `Deferred`
- `RequiresAdapter`
- `RuntimeSafe`

Risky generic cases fail closed. Inactive rollback targets, terminal quest state, created references, location aliases, quest-object aliases, unresolved reference aliases, and controller-like alias topology require an adapter. Resolved world aliases or an active scene participant are deferred.

`StageOnly` is only a candidate description. It is not permission to call `SetStage`.

## Exact adapter authorization

Ordinary callers cannot construct a verified runtime-safety token.

`RuntimeSafe` requires `PartyQuestRuntimeCompatibilityPolicy` to match a quest-specific compatibility requirement against local evidence for:

- stable `QuestId`
- compatibility profile version
- resolved-record fingerprint
- winning-override fingerprint
- script fingerprint
- native-adapter fingerprint

Unknown or incomplete evidence blocks authorization.

## Transactional runtime lifecycle

`PartyQuestRuntimeApplyCoordinator` models the future critical repair sequence without calling Skyrim:

1. validate a `RuntimeSafe` plan;
2. wait in `DeferredWorld` without holding the save guard when world targets are unavailable;
3. activate the critical save guard when targets are ready;
4. require a pre-repair checkpoint;
5. arm the durable `RuntimeMutationMayHaveOccurred` marker;
6. only after that marker is durable may a future executor dispatch a Skyrim/Papyrus mutation;
7. wait for Papyrus/event-queue quiescence;
8. resnapshot the quest;
9. require two consecutive canonical digests;
10. durably commit the transaction before releasing the critical section.

Duplicate transaction ids are idempotent when their fingerprints match. Reusing an id for different content is a conflict.

## Crash recovery

The runtime side-effect journal is separate from canonical campaign-state persistence.

It stores:

- `CampaignId`
- stable local `PlayerProfileId`
- committed runtime transaction fingerprints
- optional in-progress critical-repair state

The journal is intentionally scoped to both campaign and local player profile. Copying another character's sidecar cannot suppress or replay this character's runtime application.

A crash after runtime mutation may have occurred blocks all new runtime application until the external pre-repair/LastKnownGood checkpoint has actually been restored.

Pre-mutation stale work is discarded after restart so the client can request/build a fresh plan from the current canonical state. Deferred-world work may resume because no mutation or save guard was active yet.

## Conservative sidecar recovery

The runtime side-effect journal does **not** silently fall back to an older `.bak` file.

That would be unsafe because the backup can predate a newer armed or committed runtime transaction, causing duplicate Skyrim side effects after restart.

Load policy:

1. valid primary archive: use it;
2. failed/missing primary with a valid fully written `.tmp`: use the temporary archive as the newest complete journal after an interrupted atomic replacement;
3. only a valid older `.bak`: return `BackupRecoveryRequired` and require explicit checkpoint recovery;
4. otherwise fail closed.

The archive is checksum-protected, uses overflow-safe length parsing, and rejects recovery data belonging to another campaign or player profile.

## Isolated co-op save layout

`PartyQuestCoopSaveLayout` is a pure path planner. It performs no file writes.

The planned tree is:

```text
CoopCampaigns/
  Campaign_<32 hex CampaignId>/
    Player_<32 hex PlayerProfileId>/
      checkpoints/
        PreJoin/
        PreMigration/
        PreRepair/
        SessionStart/
        LastKnownGood/
      saves/
      sidecars/
        party_quest_runtime_apply.bin
      metadata/
```

A stable `PartyQuestPlayerProfileId` has its own immutable metadata persistence with atomic replacement and safe backup recovery. The player profile identity is independent of transient network `PlayerId`.

Original solo saves are intentionally outside this layout.

## Remaining work before any canonical mutation

The control plane still needs concrete client integrations for:

- creation and copying of separate co-op saves plus relevant sidecars;
- actual save blocking during a critical repair;
- creation/restoration of PreRepair and LastKnownGood checkpoints;
- unloaded-cell/world-target readiness callbacks;
- observable Papyrus/event-queue quiescence;
- real mod/script/native-adapter fingerprint collection and campaign manifest exchange;
- one combined live diagnostic validation of admission/quarantine and runtime-safety classification.

Only after those protections are implemented and validated should an executor be considered for the first narrowly scoped canonical runtime mutation.
