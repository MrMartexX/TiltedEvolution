# Equal-party runtime apply guardrails

This document tracks the protection/control-plane work required before the new server-authoritative quest protocol is allowed to mutate Skyrim runtime state.

## Current safety boundary

The canonical quest path still does not execute `TESQuest::ScriptSetStage`, restore aliases, mutate inventories, change quest objects, or apply canonical quest state to a running Skyrim process.

`PartyQuestApplyPlan::DryRunOnly` remains `true`.

The branch now has both game-independent recovery/filesystem primitives and a narrow Skyrim save boundary:

- verified co-op replica copy/checkpoint/restore code;
- a real `BGSSaveLoadManager::Save_Impl` interception layer;
- a transaction-scoped process save guard;
- a scoped canonical `sLocalSavePath:General` override for isolated co-op saves;
- a controlled helper that can create and verify a fresh core `.ess` plus an SKSE `.skse` when one is produced.

The new core-save helper does **not** mark a PreRepair checkpoint complete and does not authorize runtime mutation. External sidecar coverage and the durable checkpoint manifest still have to be composed before `MarkCheckpointCreated()` may occur.

Solo saves remain outside the writable co-op replica tree.

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

- stable `QuestId`;
- compatibility profile version;
- resolved-record fingerprint;
- winning-override fingerprint;
- script fingerprint;
- native-adapter fingerprint.

Unknown or incomplete evidence blocks authorization.

## Transactional runtime lifecycle

`PartyQuestRuntimeApplyCoordinator` models the future critical repair sequence:

1. validate a `RuntimeSafe` plan;
2. wait in `DeferredWorld` without holding the save guard when world targets are unavailable;
3. activate the critical physical save guard when targets are ready;
4. require a complete durable PreRepair checkpoint;
5. arm the durable `RuntimeMutationMayHaveOccurred` marker;
6. only after that marker is durable may a future executor dispatch a Skyrim/Papyrus mutation;
7. wait for Papyrus/event-queue quiescence;
8. resnapshot the quest;
9. require two consecutive canonical digests;
10. durably commit the transaction before releasing the critical section.

Duplicate transaction ids are idempotent when their fingerprints match. Reusing an id for different content is a conflict.

## Live Skyrim save guard

`PartyQuestSaveGuard` is no longer policy-only. `Code/client/Games/Skyrim/SaveLoad.cpp` hooks Skyrim's `BGSSaveLoadManager::Save_Impl` at the Address Library relocation used by the STR runtime (`AE-side id 35727`; CommonLibSSE-NG pair `34818/35727`).

Every allowed engine save acquires a shared RAII permit for the complete original save call. Critical repair `Acquire()`/`Release()` use the exclusive side of the same gate. Consequently:

- a repair lease cannot become active halfway through an already-running save;
- after a repair lease is active, a new ordinary engine save cannot enter;
- manual, auto and quick save requests are all denied at the same engine entry point;
- a controlled checkpoint must hold an exact transaction-bound `PartyQuestControlledSaveScope`;
- controlled authorization is thread-local and cannot be inherited by another thread;
- a stale/wrong `TransactionId` cannot authorize a later repair.

Production `PartyQuestRuntimeGuardedSession` binds to `PartyQuestSaveGuard::GetProcessGuard()` by default so its control plane and the Skyrim detour observe the same physical lease. The explicit alternate-guard constructor exists only for isolated tests.

## Skyrim/SKSE save sequencing

The controlled path uses the normal manual-save contract `Save_Impl(2, 0, fileName)`.

SKSE's save hooks establish the important ordering inside that call:

1. the save name is set for serialization;
2. SKSE dispatches its save-game message;
3. the actual Skyrim save pipeline runs;
4. during Skyrim VM global-data serialization, SKSE synchronously writes the `.skse` co-save and invokes plugin serialization callbacks;
5. the save call unwinds and SKSE clears the active save name.

Therefore the engine-save permit and the scoped save-path override remain alive across both the `.ess` serialization and the SKSE co-save path for this controlled call. The SKSE save-game message is not treated as a post-`.ess` completion signal.

`BGSSaveLoadManager::SaveByName()` intentionally calls the live hooked relocation, never the original trampoline, so the controlled path cannot bypass its own save guard.

## Isolated Skyrim save path

`PartyQuestSkyrimSavePathPolicy` defines the only runtime path shape exposed through `sLocalSavePath:General`:

```text
CoopCampaigns\Campaign_<32HEX>\Player_<32HEX>\saves\
```

The policy rejects absolute paths, UNC/device paths, traversal, ambiguous slash normalization and invalid ids.

`PartyQuestSkyrimSavePathScope` resolves the live `sLocalSavePath:General` setting through the verified `INISettingCollection` runtime layout, serializes our process-local override with a mutex, supplies scope-owned stable string storage, and restores the original pointer on exit when it still owns the live value.

The physical `PartyQuestCoopSavePaths` supplied to runtime capture must match the same campaign/player layout and point at an existing absolute `.../CoopCampaigns/.../saves` directory. The capture helper verifies the expected files at that physical path after Skyrim returns; a mismatched base-directory mapping therefore fails closed instead of silently accepting a save written to the normal solo `Saves` directory.

## Controlled PreRepair core-save source

`PartyQuestSkyrimPreRepairSave::CaptureCoreSource()` is a pre-checkpoint source producer, not the checkpoint itself.

It is allowed only while the active runtime transaction is:

- in `AwaitingCheckpoint`;
- holding the matching process save guard;
- not already checkpointed;
- still before any possible runtime mutation.

Each capture uses a fresh create-new name containing transaction id, target revision and an attempt nonce. Existing `.ess`/`.skse` files are never adopted merely because they exist: a previous crash may have left a partial engine save. Old failed attempts remain confined to the co-op replica for later retention/cleanup policy.

After a successful engine return:

- the `.ess` is mandatory and must be a regular non-symlink file;
- the source is re-read through `PartyQuestReplicaFileExecutor::InspectSource()` and receives size/digest evidence;
- an `.skse` file, when produced, is independently inspected and included;
- the helper returns `PartyQuestReplicaFileSpec` entries only.

It deliberately cannot call `MarkCheckpointCreated()`. Required external DLL/JSON/RaceMenu/other sidecars must first be collected according to compatibility policy. Only the complete file set may be turned into an immutable revision checkpoint and passed through `PartyQuestRuntimeGuardedSession::EnsurePreRepairCheckpoint()`.

## Deferred world targets

`PartyQuestDeferredWorldQueue` keeps canonical repairs that require loaded world/reference state out of the active mutation sequence until runtime hooks explicitly confirm their targets are ready.

A newer canonical quest revision replaces older deferred work for the same quest. A stale repair is therefore not allowed to execute minutes later merely because its cell eventually loads.

No existing runtime hook currently declares a real Skyrim cell/reference ready; that integration remains ahead.

## Papyrus quiescence

`PartyQuestPapyrusQuiescenceTracker` requires consecutive samples where:

- the observed pending event count is zero;
- the quest-event generation remains unchanged.

Queued work or a newly observed quest event resets stability. This is the deterministic gate the future client hook must drive before post-apply resnapshot verification begins.

The tracker exists, but real Papyrus/event-queue observations are not connected yet.

## Crash recovery journal

The runtime side-effect journal is separate from canonical campaign-state persistence.

It stores:

- `CampaignId`;
- stable local `PlayerProfileId`;
- committed runtime transaction fingerprints;
- optional in-progress critical-repair state.

The journal is scoped to both campaign and local player profile. Copying another character's sidecar cannot suppress or replay this character's runtime application.

A crash after runtime mutation may have occurred blocks all new runtime application until the external PreRepair/LastKnownGood checkpoint has actually been restored.

Pre-mutation stale work is discarded after restart so the client can request/build a fresh plan from current canonical state. Deferred-world work may resume because no mutation or save guard was active yet.

## Conservative sidecar recovery

The runtime side-effect journal does **not** silently fall back to an older `.bak` file.

A backup can predate a newer armed or committed runtime transaction, so rolling the journal backward could allow duplicate Skyrim side effects.

Load policy:

1. valid primary archive: use it;
2. failed/missing primary with a valid fully written `.tmp`: use the temporary archive as the newest complete journal after an interrupted atomic replacement;
3. only a valid older `.bak`: return `BackupRecoveryRequired` and require explicit checkpoint recovery;
4. otherwise fail closed.

The archive is checksum-protected, uses overflow-safe length parsing, and rejects recovery data belonging to another campaign or player profile.

## Isolated co-op replica filesystem

The player tree is:

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
        external/
      metadata/
        replica_manifest.bin
        restore/
```

`PartyQuestReplicaFileExecutor` performs verified create-only copies when explicitly invoked. It rechecks source bytes, stages the complete set into temporary siblings, verifies those temporary files and only then publishes final paths. Normal in-process publication failure rolls back files created by that call.

Import sources are required to be outside the player replica. Checkpoint sources are required to be inside the player replica but outside the checkpoint tree. Destination confinement is rechecked independently of the pure planner.

The file checksum is a local integrity checksum, not an adversarial authentication primitive.

## Durable replica/checkpoint completion

`PartyQuestReplicaManifestStore` treats copied files and a completed snapshot as different concepts.

A multi-file snapshot becomes usable only after a checksum-protected manifest records:

- campaign and player identity;
- snapshot/checkpoint type;
- campaign revision;
- relative files;
- expected sizes and digests.

Future validation reloads the manifest and verifies the published files again. A valid interrupted `.tmp` wins over an older backup; backup-only state is surfaced as recovery-required.

`PartyQuestReplicaSnapshotManager` composes copy, verification and manifest durability. It can safely finish one crash window where all exact files were already published but the manifest was not. Partial/conflicting orphan copies fail closed.

## Immutable revision checkpoints

Production-oriented checkpoints are published under immutable revision directories such as:

```text
checkpoints/PreRepair/Revision_000000000000019A/
```

`PartyQuestReplicaFileExecutor::ExecuteRevisionCheckpoint` and `PartyQuestReplicaSnapshotManager::EnsureRevisionCheckpoint` keep repeated recovery points from overwriting an earlier revision. Legacy kind-root checkpoint APIs remain only for existing archive/test tooling.

Retention and checkpoint-selection policy are still higher-level concerns.

## Crash-resumable checkpoint restore

`PartyQuestReplicaRestorePlanner` accepts only a checkpoint whose manifest and bytes verify for the expected campaign/player. Restore destinations remain restricted to the current co-op replica's `saves/` and `sidecars/external/` roots.

`PartyQuestReplicaRestoreJournal` records the exact pre-mutation observations and rollback locations. `PartyQuestReplicaRestoreExecutor` completes the filesystem transaction:

1. persist `Prepared`;
2. create and verify rollback copies;
3. persist `BackupsReady`;
4. stage and verify all checkpoint files;
5. recheck live destination drift;
6. persist `MutationStarted` before the first live replacement;
7. replace and verify the complete target set;
8. persist `Restored` and `Committed`.

A crash at `MutationStarted` is recovered conservatively by restoring the full pre-mutation file set and terminating that attempt. A `Restored` transaction is re-verified before commit. Backup-only journal recovery remains fail-closed.

The executor is still not invoked automatically by Skyrim; it remains a protected primitive until the recovery/bootstrap lifecycle is wired.

## Remaining work before canonical Skyrim mutation

The important remaining blockers are now concentrated at the game/runtime boundary:

- validate the new controlled core-save capture under CI and then in live Skyrim diagnostics;
- implement explicit external-sidecar discovery/requirements and compose the complete file set into the immutable PreRepair checkpoint gate;
- establish the production Skyrim user-directory/replica bootstrap and prove the physical `CoopCampaigns` root before activating runtime capture;
- connect crash-recovery checkpoint selection and restore execution to the live client lifecycle;
- wire unloaded-cell/reference readiness to `PartyQuestDeferredWorldQueue`;
- wire observable Papyrus/event-queue activity to `PartyQuestPapyrusQuiescenceTracker`;
- collect real mod/script/native-adapter fingerprints and exchange the campaign compatibility manifest;
- run combined live diagnostics for admission/quarantine, runtime-safety buckets, isolated save capture, checkpoint creation and restore recovery.

Only after those protections are implemented and validated should the first narrowly scoped canonical runtime mutation be considered.
