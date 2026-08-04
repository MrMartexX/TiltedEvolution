# Runtime crash recovery orchestration

`PartyQuestRuntimeRecoveryCoordinator` bridges the durable runtime-apply barrier and the crash-resumable co-op replica restore executor without calling Skyrim, Papyrus, save or load APIs.

## Exact checkpoint rule

A blocked runtime repair currently has one admissible recovery source:

```text
checkpoints/PreRepair/Revision_<TargetWorldRevision>/
```

The coordinator does not guess or select a `LastKnownGood` fallback. The runtime recovery record stores `TargetWorldRevision`, but it does not yet store an explicit independently selected fallback checkpoint revision. Using an arbitrary older snapshot could therefore roll unrelated local state backward.

Before any replica mutation, recovery requires:

- the same `CampaignId` and stable `PlayerProfileId` as the runtime session;
- a valid blocked recovery record with `CheckpointCreated=true` and `RuntimeMutationMayHaveOccurred=true`;
- a durable `RevisionCheckpoint` manifest;
- `CheckpointKind=PreRepair`;
- `CampaignWorldRevision=TargetWorldRevision`;
- successful manifest byte verification;
- a confined restore plan produced by `PartyQuestReplicaRestorePlanner`.

Missing, stale-backup-only, invalid, mismatched or corrupt checkpoint state fails closed and leaves the runtime recovery barrier active.

## Restore transaction reuse

The caller supplies a non-zero restore id. That id identifies a durable filesystem restore transaction under:

```text
metadata/restore/Transaction_<RestoreId>/journal.bin
```

If no journal exists, the coordinator starts `PartyQuestReplicaRestoreExecutor::Execute`.

If a valid journal already exists, the coordinator first proves that the journal describes the exact same campaign/player/checkpoint revision and restore operations, then calls `PartyQuestReplicaRestoreExecutor::Recover` instead of creating a second transaction.

This makes the orchestration idempotent across process or persistence failures.

## Two independent durability barriers

Successful crash recovery has two separate durable facts:

1. checkpoint bytes are restored and the restore journal is committed;
2. the runtime-apply journal no longer contains the crash recovery barrier.

The second fact is written only after the first has been proven.

If checkpoint restore commits but `CompleteCrashCheckpointRestore()` cannot persist the cleared runtime state, the live replica remains restored while the in-memory/runtime journal barrier remains blocked. A later call with the same restore id loads the committed restore journal, receives `AlreadyCommitted`, re-verifies the restored files, and retries only the runtime-journal transition.

The checkpoint is not copied again.

## Interrupted restore semantics

A restore journal in `MutationStarted` does not prove that the requested checkpoint is present. The restore executor conservatively rolls the replica back to the pre-restore bytes and returns `RecoveredRollback`.

`PartyQuestRuntimeRecoveryCoordinator` maps that result to `RollbackRecoveredRetryRequired` and deliberately leaves the quest runtime recovery barrier active.

A later call may then start a fresh exact checkpoint restore using the same restore id after the rolled-back transaction directory has been retired.

This prevents a safe rollback of the restore mechanism itself from being confused with successful recovery of the earlier Skyrim quest mutation.

## Current boundary

This coordinator is still game-independent. It does not:

- intercept Skyrim startup or load;
- automatically discover `.ess`/`.skse` files from a running game;
- choose between several historical checkpoints;
- execute `TESQuest::ScriptSetStage`;
- restore aliases, scenes or inventory;
- declare Papyrus quiescence.

It provides the durable recovery primitive that those later client integrations can call.

## Test coverage

`party_quest_runtime_recovery.cpp` covers:

- exact `PreRepair` revision restore followed by durable barrier clearance;
- refusal to substitute `LastKnownGood` when the exact `PreRepair` revision is absent;
- checkpoint restore commit followed by runtime-journal persistence failure and idempotent retry;
- an existing `MutationStarted` restore journal that first rolls back and keeps the runtime barrier, followed by a fresh exact restore that finally clears it.
