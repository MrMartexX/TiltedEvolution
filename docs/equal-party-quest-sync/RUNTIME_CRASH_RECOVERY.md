# Runtime crash recovery orchestration

Document status: active subsystem design. Acceptance of the complete production
recovery path is tracked in [CURRENT_STATUS.md](../project/CURRENT_STATUS.md),
not inferred from the existence of the coordinator described here.

`PartyQuestRuntimeRecoveryCoordinator` and `PartyQuestRuntimeLiveRecoveryCoordinator` bridge the durable runtime-apply barrier and the co-op replica restore executors without calling Skyrim, Papyrus, save or load APIs. Fresh crash and live recoveries on Linux/POSIX and Windows NTFS route through `PartyQuestRuntimeRestoreAttemptStore`, `PartyQuestReplicaDurableRestorePreparation`, and `PartyQuestReplicaDurableRestoreExecutor` (`PowerLossDurable`), while retaining fail-closed domain fences and backwards-compatible recovery for pre-existing legacy v3 journals.

## Exact checkpoint rule

A blocked runtime repair currently has one admissible recovery source:

```text
checkpoints/PreRepair/Revision_<TargetWorldRevision>/
```

The coordinator does not guess or select a `LastKnownGood` fallback. The runtime recovery record stores `TargetWorldRevision`, but it does not yet store an explicit independently selected fallback checkpoint revision. Using an arbitrary older snapshot could therefore roll unrelated local state backward.

Before any replica mutation, recovery requires:

- the same `CampaignId` and stable `PlayerProfileId` as the runtime session;
- a valid blocked recovery record with `CheckpointCreated=true` and `RuntimeMutationMayHaveOccurred=true`;
- a `RevisionCheckpoint` manifest for the exact `PreRepair` revision;
- `CheckpointKind=PreRepair`;
- `CampaignWorldRevision=TargetWorldRevision`;
- successful manifest byte verification;
- a confined restore plan produced by `PartyQuestReplicaRestorePlanner`.

Missing, stale-backup-only, invalid, mismatched or corrupt checkpoint state fails closed and leaves the runtime recovery barrier active.

## Restore transaction identity and durable attempt retry contract

Crash and live recovery separate the stable higher-level repair transaction identity from each filesystem restore attempt:

```text
RuntimeTransactionId = stable higher-level repair identity
RestoreAttemptId     = unique monotonic identity of one filesystem restore attempt
```

`PartyQuestRuntimeRestoreAttemptStore` persists this binding under:

```text
metadata/restore_attempts/Transaction_<TransactionId>/attempt.bin
```

Each strong v4 restore attempt writes its journal under `metadata/restore/Transaction_<RestoreAttemptId>/journal.bin`. When an interrupted strong attempt rolls back after `MutationStarted`, it terminates in a permanent `RolledBack` tombstone for that `RestoreAttemptId` and `AdvanceAfterRolledBackAuthorized` durably advances `CurrentAttemptId` to `NextAttemptId` so a subsequent retry executes a fresh attempt without deleting or reusing the terminal tombstone. Pre-existing legacy v3 journals at `Transaction_<TransactionId>` remain supported only when no strong attempt store is present.

## Persisted journal durability-domain fence

Restore journal archive versions now identify the persistence protocol when that fact is knowable:

- v1/v2: historical, readable but durability origin is ambiguous;
- v3: explicit process-crash `SaveAtomically` / `Load` domain;
- v4: explicit power-loss-durable `SavePowerLossDurably` / `LoadPowerLossDurably` domain.

Current runtime crash and live recovery call the legacy `Load()` before the legacy executor. `Load()` accepts only explicit v3 journals. Consequently:

- a v4 strong journal returns `DurabilityMismatch` and maps to `RestoreJournalConflict`;
- a v1/v2 journal returns `DurabilityAmbiguous` and maps to `RestoreJournalConflict`;
- neither case enters `PartyQuestReplicaRestoreExecutor::RecoverAuthorized`;
- live replica bytes remain untouched by the recovery coordinator;
- the runtime recovery barrier remains active.

This is intentional. `Decode()` can inspect historical evidence, but inspection does not grant an executor recovery authority. There is no automatic v1/v2 migration because those archives do not encode which publication protocol produced them.

The journal writers also preflight valid primary/`.tmp`/`.bak` evidence and refuse to discard valid evidence belonging to the other or ambiguous domain.

## Two independent durability barriers

Successful current crash recovery has two separate facts:

1. checkpoint bytes are restored and the legacy restore journal is committed;
2. the runtime-apply journal no longer contains the crash recovery barrier.

The second fact is written only after the first has been proven.

A committed restore journal alone is not accepted as proof that the live replica is still correct. Immediately before clearing the runtime barrier, the coordinator independently re-observes every live restore destination and requires the exact size and digest from the restore plan. If another actor changed a live file after restore commit, recovery fails closed and the barrier remains active.

If checkpoint restore commits but `CompleteCrashCheckpointRestore()` cannot persist the cleared runtime state, the live replica remains restored while the in-memory/runtime journal barrier remains blocked. A later call loads the transaction-id-bound committed v3 restore journal, receives `AlreadyCommitted`, re-verifies the live files, and retries only the runtime-journal transition.

The checkpoint is not copied again.

## Interrupted legacy restore semantics

A legacy restore journal in `MutationStarted` does not prove that the requested checkpoint is present. The legacy restore executor conservatively rolls the replica back to the pre-restore bytes and returns `RecoveredRollback`.

`PartyQuestRuntimeRecoveryCoordinator` maps that result to `RollbackRecoveredRetryRequired` and deliberately leaves the quest runtime recovery barrier active.

A later call may then start a fresh exact checkpoint restore using the same transaction id because the legacy rollback path retires that restore directory. This behavior is explicitly legacy-domain semantics and must not be projected onto strong terminal `RolledBack`.

## Strong production routing remains blocked

The power-loss-durable executor is not a drop-in replacement for the current calls. Before production routing may select it, the project needs at least:

1. a restore-attempt identity/retry contract compatible with permanent terminal tombstones;
2. an owner/coordinator rule selecting the strong path only from explicit v4 evidence;
3. durable directory namespace establishment for the production restore tree;
4. Linux production recovery wiring under the exact campaign/player workspace owner/lease;
5. continued Windows fail-closed behavior until directory/delete durability semantics are accepted;
6. physical filesystem/device power-loss validation under documented assumptions.

None of these requirements grants native Skyrim mutation authority.

## Current boundary

This coordinator is still game-independent. It does not:

- enable canonical `TESQuest::SetStage`;
- restore aliases, scenes or inventory;
- choose between historical checkpoints;
- infer Papyrus quiescence;
- accept reference readiness as mutation authority;
- raise `PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee` above `ProcessCrashResilient`;
- make `AllowsNativeRuntimeMutation()` true.

## Test coverage

Runtime recovery tests now cover:

- exact `PreRepair` revision restore followed by durable barrier clearance;
- refusal to substitute `LastKnownGood` when the exact `PreRepair` revision is absent;
- checkpoint restore commit followed by runtime-journal persistence failure and idempotent retry;
- an existing legacy `MutationStarted` journal that rolls back, keeps the runtime barrier and later retries the exact restore;
- live-replica drift after a committed restore, detected before the runtime barrier is cleared;
- explicit v4 strong journal rejection by the production legacy crash-recovery coordinator before legacy executor entry;
- ambiguous v2 journal rejection without adoption or live replica mutation.

The last two tests prove the current fail-closed domain boundary; they do not prove that strong production routing is ready.
