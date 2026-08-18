# Equal-Party Persistence Durability Boundary

## Status

The production native-mutation durability requirement is `PowerLossDurable`.
The current implementation still advertises only `ProcessCrashResilient` through
`PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee`.

This document is an ordering inventory and closure checklist. It is not evidence
that P0-H is closed. `PartyQuestStableStorage` provides narrow OS flush
primitives only; no caller currently gains mutation authority merely because
those primitives exist.

## Authoritative persistence graph

Power-loss durability must cover the complete graph, not only files whose class
names contain `Persistence`.

### Canonical/runtime metadata

The following stores currently use complete temporary files plus verification
and rename/backup publication:

- `PartyQuestCampaignPersistence`
- `PartyQuestPlayerProfilePersistence`
- `PartyQuestStatePersistence`
- `PartyQuestRuntimeApplyPersistence`
- `PartyQuestReplicaManifestStore`
- `PartyQuestReplicaRestoreJournalPersistence`

Their existing protocols provide process-crash recovery semantics. Stream flush
and a successful rename are not, by themselves, the project's
`PowerLossDurable` proof.

### Replica/checkpoint data files

`PartyQuestReplicaFileExecutor` stages each replica/checkpoint data file,
verifies its size/digest, renames it to the final destination and verifies the
final file. Those data files include the protected replica's Skyrim save,
SKSE co-save and required external sidecars.

`PartyQuestReplicaSnapshotManager` publishes the manifest only after all final
data files verify. The manifest is therefore the authority marker for a
complete snapshot. A power-loss-safe implementation must preserve the stronger
ordering:

1. write/copy every staged data file;
2. force each staged file's bytes to stable storage;
3. publish each final data-file directory entry;
4. force the affected directory metadata required to make those publications
   persistent;
5. only then write, verify and durably publish the manifest;
6. only then treat the snapshot as authoritative.

The manifest must never become durably authoritative while any file it names is
still only cache-resident or its final directory entry can be lost independently.

### Destructive restore

`PartyQuestReplicaRestoreExecutor` currently enforces this logical recovery
sequence:

1. persist `Prepared` journal state;
2. create and verify rollback backups;
3. persist `BackupsReady`;
4. stage and verify checkpoint bytes;
5. persist `MutationStarted`;
6. replace replica destinations;
7. verify restored destinations;
8. persist `Restored`;
9. persist `Committed`;
10. clean temporary/transaction files when safe.

For `PowerLossDurable`, every journal phase that authorizes the next destructive
step must itself be durably ordered after the filesystem evidence on which that
phase depends. In particular:

- rollback backup bytes and their published names must be durable before a
  durable `BackupsReady` record;
- the durable `MutationStarted` record must precede destructive destination
  replacement;
- each restored destination's bytes and published name must be durable before a
  durable `Restored` record;
- `Committed` must not become durable before all required restored-state
  durability barriers complete.

Rollback/recovery publication has the same requirement in reverse: restored
original bytes and namespace changes must be durable before recovery can claim
that original state has been restored.

## OS primitive boundary

`PartyQuestStableStorage::FlushFile` supplies an explicit file flush primitive:

- Windows: `FlushFileBuffers` on an existing file handle opened for write;
- Linux: `fsync` on an opened file descriptor.

`PartyQuestStableStorage::FlushDirectory` / `FlushParentDirectory` currently:

- Linux: use `fsync` on a directory descriptor;
- Windows: return `Unsupported` deliberately.

The Windows result is fail-closed. The project does not infer a POSIX-style
parent-directory durability contract from undocumented behavior or from a
mechanism that would require running Skyrim elevated.

## P0-H closure conditions

Do not raise `CurrentLocalGuarantee` to `PowerLossDurable` until all of the
following are true:

1. every authoritative metadata store uses a reviewed stable-storage publication
   protocol;
2. replica/checkpoint data-file publication is durably ordered before manifest
   authority publication;
3. restore rollback backups, destructive replacements and every journal phase
   transition obey the ordering above;
4. recovery/adoption paths use the same durability contract rather than silently
   downgrading it after a crash;
5. Linux file + directory ordering is covered by deterministic failure/fault
   tests at every durable boundary;
6. a documented, non-admin Windows publication protocol provides equivalent
   required guarantees, or Windows remains explicitly unable to satisfy the
   production mutation gate;
7. cross-platform CI is green and the final live validation matrix records the
   exact tested SHA and filesystem/runtime assumptions.

Until then, `AllowsNativeRuntimeMutation()` must remain false and canonical
Skyrim mutation must remain disabled.
