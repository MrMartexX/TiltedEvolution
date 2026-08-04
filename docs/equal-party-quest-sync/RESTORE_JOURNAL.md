# Crash-safe co-op replica restore journal

This milestone remains game-independent: it can replace files only inside the isolated co-op replica when explicitly invoked, but it is **not** connected to Skyrim save/load hooks or live quest mutation.

`PartyQuestReplicaRestoreJournal` provides the durable control plane and `PartyQuestReplicaRestoreExecutor` now enforces that control plane while restoring a verified checkpoint.

## Transaction boundary

A restore transaction is bound to:

- `CampaignId`;
- stable `PlayerProfileId`;
- a non-zero restore ID;
- checkpoint kind;
- non-zero canonical `CampaignWorldRevision`;
- the exact verified checkpoint source and replica destination set.

Rollback data is routed only under the player's metadata tree:

```text
Player_<PlayerProfileId>/
  metadata/
    restore/
      Transaction_<RestoreId>/
        journal.bin
        rollback/
          saves/...
          sidecars/external/...
```

Solo-save paths are never restore destinations or rollback targets.

## Durable phases

The journal state machine is:

```text
Prepared
  -> BackupsReady
  -> MutationStarted
  -> Restored
  -> Committed
```

`MutationStarted` is the durable mutation barrier. The executor persists that phase before replacing the first live co-op replica file.

Recovery disposition is deliberately conservative:

- `Prepared` / `BackupsReady`: resume safely because no live mutation was allowed yet;
- `MutationStarted`: restore the pre-mutation bytes first and terminate that restore attempt;
- `Restored`: verify the restored targets and then durably commit;
- `Committed`: clean/idempotent.

A transition to `BackupsReady` requires every originally existing destination to have a verified rollback copy with the original size and digest. Destinations that did not originally exist must not invent rollback bytes.

A transition to `Restored` or `Committed` requires all live replica targets to match the checkpoint size and digest.

## Executor ordering

A fresh restore follows this sequence:

1. prepare and persist the journal;
2. create verified rollback copies for every existing destination;
3. persist `BackupsReady`;
4. stage and verify every checkpoint source beside its final destination without touching the live file;
5. re-verify that live destinations still match the observations captured by `Prepared`;
6. mark and persist `MutationStarted`;
7. replace destinations through same-directory staged renames;
8. verify the complete restored file set;
9. persist `Restored`;
10. persist `Committed`;
11. remove only executor-owned sibling staging files.

If replacement or verification fails after the mutation barrier, the executor restores all original destinations from the verified rollback set. Originally absent files are removed again. The rollback is verified before the transaction directory is retired.

The executor independently rechecks path confinement and refuses symlink/non-regular-file destinations. It accepts checkpoint sources only from the selected checkpoint tree and destinations only from the current player's `saves/` or `sidecars/external/` roots. `party_quest_runtime_apply.bin`, checkpoint storage, metadata and solo-save paths are not legal restore destinations.

## Crash recovery policy

Recovery intentionally does not guess how far a `MutationStarted` restore progressed. Even if some destination already contains canonical bytes, the entire attempt is rolled back to the captured pre-mutation state and terminated. A later caller can build a fresh restore plan from current canonical state.

This prevents a process restart from replaying an unknown subset of file side effects.

A `Restored` journal is different: all target bytes had already verified before that phase could be persisted, so recovery re-verifies them and advances to `Committed` if they still match. A mismatch fails closed and rolls back.

## Persistence semantics

The journal uses checksum-protected atomic persistence with `.tmp` and `.bak` files.

A fully written valid `.tmp` may recover a crash during journal publication because it represents the newer complete state. A stale `.bak` is never silently promoted to current truth. If only the backup is trustworthy, loading returns `BackupRecoveryRequired`; the executor does not mutate replica files from that stale state.

## Current safety boundary

The restore executor now performs real filesystem replacement, but only inside the isolated co-op replica tree and only when explicitly invoked. It still does **not**:

- intercept Skyrim save/load entry points;
- overwrite original solo saves;
- call `TESQuest::ScriptSetStage`;
- restore aliases;
- mutate inventory or quest objects;
- automatically recover a running Skyrim process.

Those integrations remain blocked on the higher-level save guard, runtime compatibility, deferred-world and Papyrus-quiescence work.

## Validation coverage

`party_quest_replica_restore_executor.cpp` covers:

- successful restore with a verified rollback copy;
- restore of a destination that did not previously exist;
- protection of an unrelated solo-save file;
- destination drift detected before the mutation barrier;
- simulated `MutationStarted` crash recovery with verified rollback and transaction retirement;
- `Restored` recovery that verifies bytes before durable commit.

Existing journal tests continue to cover checksum persistence, temporary-file recovery and fail-closed stale-backup handling.

## Next integration boundary

The filesystem transaction is no longer the blocker by itself. Before the executor can be called automatically during a live co-op session, the project still needs to connect:

- real Skyrim save blocking to `PartyQuestSaveGuard`;
- checkpoint creation to the isolated co-op replica flow;
- runtime-apply recovery decisions to checkpoint restore selection;
- unloaded-cell/reference readiness to `PartyQuestDeferredWorldQueue`;
- observable Papyrus/event-queue activity to `PartyQuestPapyrusQuiescenceTracker`;
- real campaign compatibility fingerprints and adapter authorization.

Only after those integrations are validated should canonical quest mutation be enabled.