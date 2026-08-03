# Crash-safe co-op replica restore journal

This milestone remains game-independent and does **not** overwrite Skyrim saves at runtime.

`PartyQuestReplicaRestoreJournal` is the durable control plane that a future destructive restore executor must obey before replacing any file in the isolated co-op replica.

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

The `MutationStarted` phase is the durable mutation barrier. A future executor must persist that phase before replacing the first live co-op replica file.

Recovery disposition is deliberately conservative:

- `Prepared` / `BackupsReady`: resume safely before mutation;
- `MutationStarted`: rollback required;
- `Restored`: verify restored targets before commit;
- `Committed`: clean.

A transition to `BackupsReady` requires every originally existing destination to have a verified rollback copy with the original size and digest. Destinations that did not originally exist must not invent rollback bytes.

A transition to `Restored` or `Committed` requires all live replica targets to match the checkpoint size and digest.

## Persistence semantics

The journal uses checksum-protected atomic persistence with `.tmp` and `.bak` files.

A fully written valid `.tmp` may recover a crash during journal publication because it represents the newer complete state. A stale `.bak` is never silently promoted to current truth. If only the backup is trustworthy, loading returns `BackupRecoveryRequired` so recovery cannot accidentally forget that the mutation barrier may already have been crossed.

## Current safety boundary

No destructive restore executor is connected yet. The new code only:

- prepares a restore journal from an already verified restore plan;
- observes existing co-op replica destinations;
- defines and verifies rollback locations;
- verifies restored target bytes;
- persists/reloads the crash-recovery state machine.

The next executor milestone must implement backup creation, durable barrier persistence, atomic per-file replacement, verified rollback/resume, and cleanup while remaining confined to the current player's co-op replica.

## CI hygiene

The clean-build audit exposed stale tests left behind by API hardening rather than failures in the new runtime logic:

- `party_quest_player_scope.cpp` lacked its direct TiltedCore buffer/serialization prerequisites; those includes are now explicit;
- `party_quest_replica_copy_verifier.cpp` still targeted the removed `PartyQuestReplicaCopyVerifier` API. Verification responsibility has moved into the filesystem executor (`VerifyImport`, `VerifyCheckpoint`, and `VerifyRevisionCheckpoint`) with stronger real-file coverage, so the obsolete test was removed instead of resurrecting dead production API;
- `party_quest_runtime_apply_persistence.cpp` still used the pre-`PlayerProfileId` recovery signatures. Every recovery export/restore test is now explicitly scoped by both `CampaignId` and `PlayerProfileId`, including manually constructed recovery state.

All intermediate repair and restore-journal commits use `[skip ci]`. This commit is the single full-CI trigger after the complete blocker audit/fix pass.