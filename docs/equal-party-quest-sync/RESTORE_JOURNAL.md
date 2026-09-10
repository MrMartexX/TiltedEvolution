# Crash-safe co-op replica restore journal

This milestone remains game-independent: restore code can replace files only inside the isolated co-op replica when explicitly invoked, but the strong power-loss-durable executor is **not** connected to Skyrim save/load hooks or canonical live quest mutation.

Two filesystem restore surfaces intentionally coexist:

- `PartyQuestReplicaRestoreExecutor` is the legacy process-crash-resilient executor;
- `PartyQuestReplicaDurableRestorePreparation` plus `PartyQuestReplicaDurableRestoreExecutor` is the stronger Linux/POSIX proof surface.

Their on-disk journals are now machine-separated. A journal created by one persistence domain is not execution authority for the other executor.

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

The strong journal state machine is:

```text
Prepared
  -> BackupsReady
  -> MutationStarted
       |-> Restored -> Committed
       `-> RolledBack
```

`MutationStarted` is the durable mutation barrier. The strong executor persists that phase before replacing the first live co-op replica file.

Recovery disposition is deliberately conservative:

- `Prepared` / `BackupsReady`: return before mutation; recovery does not create a new mutation barrier;
- `MutationStarted`: restore exact pre-mutation state and durably terminate as `RolledBack`;
- `Restored`: reverify restored targets and commit; a failed restored postcondition takes the exact rollback path;
- `Committed`: verify restored postconditions and resume terminal compaction only;
- `RolledBack`: verify original postconditions and resume terminal compaction only.

A transition to `BackupsReady` requires every originally existing destination to have a verified rollback copy with the original size and digest. Destinations that did not originally exist must not invent rollback bytes.

A transition to `Restored` or `Committed` requires all live replica targets to match the checkpoint size and digest. `MarkRolledBack` is valid only from `MutationStarted` or `Restored` and independently verifies the complete original target state before changing phase.

## Strong executor ordering

A strong restore follows this sequence:

1. promote and rebind the exact immutable revision checkpoint;
2. prepare and durably persist `Prepared`;
3. create and durably verify rollback evidence;
4. durably persist `BackupsReady`;
5. re-promote/rebind the checkpoint and durably stage every source beside its final destination;
6. revalidate checkpoint sources and live destinations;
7. durably persist `MutationStarted`;
8. replace destinations through same-directory durable publication;
9. durably verify the complete restored file set;
10. durably persist `Restored`;
11. durably persist `Committed`;
12. compact executor-owned staging/rollback evidence while retaining the terminal journal and transaction directory as a RestoreId tombstone.

If replacement or verification fails after the mutation barrier, recovery restores all original destinations from the verified rollback set. Originally absent files are removed again. Existing originals are restored through durable sibling staging and atomic replacement. Only after the original-state postcondition is durably established may `RolledBack` be published and rollback evidence compacted.

The executor independently rechecks path confinement and refuses symlink/non-regular-file destinations. It accepts checkpoint sources only from the selected checkpoint tree and destinations only from the current player's `saves/` or `sidecars/external/` roots. `party_quest_runtime_apply.bin`, checkpoint storage, metadata and solo-save paths are not legal restore destinations.

## Crash recovery policy

Recovery intentionally does not guess how far a `MutationStarted` restore progressed. Even if some destination already contains checkpoint bytes, the strong attempt is rolled back to the captured pre-mutation state and terminates as `RolledBack`.

A `Restored` journal is different: all target bytes had verified before that phase could be persisted, so recovery re-verifies them and advances to `Committed` if they still match. A mismatch takes the exact rollback path.

Neither `Committed` nor `RolledBack` is deleted after compaction. Their primary journals and transaction directories remain as durable tombstones so the same RestoreId cannot become reusable merely because cleanup completed.

## Persisted durability provenance

The journal archive version is also a persistence-domain discriminator:

- **v1**: historical archive before `RolledBack`; durability origin is ambiguous;
- **v2**: historical archive with `RolledBack` support but still no persistence-domain identity; durability origin is ambiguous;
- **v3**: explicit process-crash-resilient `SaveAtomically` / `Load` domain;
- **v4**: explicit power-loss-durable `SavePowerLossDurably` / `LoadPowerLossDurably` domain.

`Decode()` can read valid v1-v4 archives for inspection. Persisted phase values `Prepared=0` through `Committed=4` keep their historical meanings; v1 claiming `RolledBack=5` is rejected.

Automatic recovery is stricter than decoding:

- legacy `Load()` accepts only v3;
- strong `LoadPowerLossDurably()` accepts only v4;
- v1/v2 return `DurabilityAmbiguous` to both loaders;
- an explicit opposite domain returns `DurabilityMismatch`;
- a wrong-domain or ambiguous valid `.tmp` is never promoted through the other protocol;
- writers preflight valid primary/`.tmp`/`.bak` evidence and refuse to delete or overwrite evidence from another or ambiguous domain.

There is deliberately **no automatic v1/v2 migration**. Old archives cannot reveal whether ordinary or stable publication created them, so assigning either durability claim would be manufactured authority.

## Production runtime recovery boundary

Current production crash/live runtime recovery still belongs to the legacy process-crash domain and calls `PartyQuestReplicaRestoreJournalPersistence::Load()` before the legacy executor. The persisted domain discriminator now makes this path fail closed:

- a v4 strong journal becomes `RestoreJournalConflict` before the legacy executor is entered;
- a v1/v2 ambiguous journal likewise becomes `RestoreJournalConflict`;
- the live co-op replica remains unchanged and the runtime recovery barrier remains active.

The strong executor is intentionally **not** routed into production yet. Current runtime recovery defines `RestoreId == TransactionId` and expects a recovered legacy rollback to permit a later retry under that same ID. Strong terminal `RolledBack` deliberately reserves its RestoreId forever as an idempotency tombstone. Production strong routing therefore first needs an explicit restore-attempt identity/retry contract that preserves the stable higher-level runtime transaction identity without reusing a terminal restore-attempt ID.

This is not solved by deleting a `RolledBack` tombstone or by treating it as permission to retry the same restore ID.

## Current safety boundary

The restore executors perform filesystem operations only inside the isolated co-op replica tree. They still do **not**:

- enable canonical `TESQuest::SetStage`;
- restore or mutate aliases;
- mutate inventory or quest objects;
- treat reference readiness as mutation authority;
- raise `PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee` above `ProcessCrashResilient`;
- make `AllowsNativeRuntimeMutation()` true.

Windows strong destructive restore remains unsupported because the accepted durable directory-tree/delete/cleanup contract is incomplete.

## Validation coverage

Current tests cover, among other cases:

- successful strong restore and terminal `Committed` tombstone;
- durable rollback to terminal `RolledBack` after a mutation-barrier cut or partial publication;
- `Restored` verification success and corrupted-`Restored` rollback;
- originally absent destinations;
- cuts after terminal publication but before compaction;
- repeated restart recovery and duplicate RestoreId rejection;
- v1/v2 decode compatibility without granting either executor authority;
- explicit v3/v4 loader separation;
- wrong-domain/ambiguous evidence preservation;
- production crash-recovery coordinator rejection of v4 and v2 before the legacy executor.

CI remains mechanism/state-machine evidence. It is not live Skyrim or physical power-cut proof.

## Next integration boundary

Before strong restore can become a production requirement, the project still needs to:

- define a stable runtime transaction identity versus unique restore-attempt identity/retry contract;
- route only explicit v4 journals to the strong recovery surface under the exact protected workspace owner/lease;
- prove production directory namespace establishment for every strong metadata/checkpoint owner;
- retain Windows fail-closed behavior until its directory/delete durability contract is accepted;
- complete real filesystem/device power-loss validation under documented assumptions;
- close the remaining P0-C/D/E/F lifecycle, provenance, Papyrus-quiescence and verification work.

Only after the full P0 matrix is actually closed may canonical quest mutation be reconsidered.