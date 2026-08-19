# Equal-Party Persistence Durability Boundary

## Status

The production native-mutation durability requirement is `PowerLossDurable`.
The current implementation still advertises only `ProcessCrashResilient` through
`PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee`.

This document is an ordering inventory and closure checklist. It is not evidence
that P0-H is closed. `PartyQuestStableStorage` provides reviewed staged-file
write and same-directory rename-publication primitives.

All six authoritative metadata stores now expose a stronger publication path:

- `PartyQuestCampaignPersistence::SavePowerLossDurably`;
- `PartyQuestPlayerProfilePersistence::SavePowerLossDurably`;
- `PartyQuestStatePersistence::SavePowerLossDurably`;
- `PartyQuestRuntimeApplyPersistence::SavePowerLossDurably`;
- `PartyQuestReplicaManifestStore::SavePowerLossDurably`;
- `PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably`.

Linux additionally has a non-destructive strong restore preparation path,
`PartyQuestReplicaDurableRestorePreparation::Prepare`. It requires an immutable
revision checkpoint to pass data-before-manifest durability promotion, durably
publishes `Prepared`, creates and verifies durable rollback copies, and durably
publishes `BackupsReady`. It deliberately stops before staging a live
replacement or publishing `MutationStarted`.

This is still not production native-mutation activation. Existing server/client
callers are not automatically converted merely because the stronger functions
exist. In particular the server canonical-state/campaign bootstrap path and the
legacy destructive restore executor still use crash-resilient writers. The
stronger path must be wired only where the surrounding directory/data ordering
is also proved.

The restore journal exposes `LoadPowerLossDurably` so recovery of a newer valid
`.tmp` does not silently downgrade to an ordinary rename. Player-profile
persistence also exposes `LoadPowerLossDurably` so a first-publication durable
`.tmp` preserves the immutable ProfileId across a crash without manufacturing
lineage authority. No caller gains native mutation authority merely because any
of these individual paths exist.

## Authoritative persistence graph

Power-loss durability must cover the complete graph, not only files whose class
names contain `Persistence`.

### Canonical/runtime metadata

The authoritative metadata stores are:

- `PartyQuestCampaignPersistence`
- `PartyQuestPlayerProfilePersistence`
- `PartyQuestStatePersistence`
- `PartyQuestRuntimeApplyPersistence`
- `PartyQuestReplicaManifestStore`
- `PartyQuestReplicaRestoreJournalPersistence`

Their legacy `SaveAtomically`-style protocols provide process-crash recovery
semantics. Stream flush and a successful rename are not, by themselves, the
project's `PowerLossDurable` proof.

The stronger writers share this core ordering:

1. require an already-existing, validated parent directory;
2. encode the complete candidate state;
3. durably create/truncate and write `.tmp`;
4. re-read/decode `.tmp` and verify exact identity/state bytes;
5. if a primary exists, durably rename primary to `.bak`, replacing an older
   backup when allowed by that store's semantics;
6. durably rename the verified `.tmp` to the primary name;
7. never perform an unproven rollback rename after a failed durable boundary.

After step 5, failure deliberately preserves the old durable state as `.bak`
and the new durable candidate as `.tmp`. After step 6, a later reported failure
must treat the new primary as potentially/durably published rather than pretend
publication did not occur.

Campaign identity has one additional invariant: the bootstrap backup is not
merely an older generation. After the primary is durably published,
`SavePowerLossDurably` durably writes/verifies a backup temporary and publishes
an exact matching v2 `.bak`, preserving `CanonicalArchiveRequired` in both
copies.

Player profile identity is immutable. `LoadPowerLossDurably` handles the strong
first-publication crash case where only `.tmp` exists. It returns that verified
identity as `UsedTemporary` without renaming it. A present invalid primary is
conflicting lineage evidence and fails closed; a valid backup must agree with a
valid temporary identity.

`PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably` is the
recovery partner for the strong restore-journal writer:

- a valid primary is returned directly;
- a valid `.tmp` is promoted only with `PublishFileRename` and only when the
  primary is absent;
- a present invalid primary plus valid `.tmp` is conflicting evidence and is
  preserved fail-closed;
- an older valid `.bak` remains `BackupRecoveryRequired`, never normal current
  truth.

### Production wiring still open

Having six strong writer APIs is not equivalent to using them end-to-end.
Production P0-H still requires each caller to prove its surrounding directory
namespace before choosing the strong writer. Current canonical server persistence
still uses legacy `PartyQuestStatePersistence::SaveAtomically` and
`PartyQuestCampaignPersistence::SaveAtomically`; switching those calls without a
cross-platform parent-directory contract would incorrectly make Windows startup
fail or overclaim durability.

Likewise, profile/runtime/manifest/journal strong APIs must only become production
requirements when their owning co-op directory tree has a compatible durable
namespace proof.

The Linux durable restore preparation is also intentionally separate from
`PartyQuestReplicaRestoreExecutor::Recover/Execute`. A `BackupsReady` journal does
not itself authorize a caller to continue through the legacy crash-resilient
`MutationStarted` path. The eventual destructive strong executor must reacquire
the exact workspace and revalidate all evidence before publishing its own durable
mutation barrier.

## Replica/checkpoint data files

`PartyQuestReplicaFileExecutor` still performs the process-crash copy protocol:
stage, verify, rename into the final destination, then verify the final file.
That executor is intentionally not relabelled as power-loss durable.

Instead, immutable revision checkpoints now have an explicit promotion path:
`PartyQuestReplicaDurableSnapshot::PromoteRevisionCheckpoint`.

On Linux/POSIX, promotion takes an already-complete revision checkpoint and
establishes the stronger ordering before any caller may treat it as
power-loss authority:

1. load the exact revision manifest;
2. verify campaign/player/revision identity and every published file;
3. durably establish the revision directory tree;
4. for every `.ess`, `.skse` and required external sidecar:
   - establish its parent directory tree durably;
   - `fsync` the final regular file;
   - `fsync` its containing directory;
   - re-read and verify exact size/digest;
5. durably establish the manifest parent directory;
6. durably republish the exact manifest through `SavePowerLossDurably`;
7. reload the manifest and re-verify every published file.

This promotion model is intentional. A crash before promotion succeeds may leave
exact orphaned data or a process-crash manifest, but neither is stronger
mutation authority. Promotion is idempotent and re-establishes every stable
barrier each time it is requested.

`PartyQuestReplicaDurableRestorePreparation` machine-enforces this dependency on
Linux. It promotes the exact revision checkpoint while holding the workspace
lease, reloads that manifest, rebuilds a restore plan from it and requires exact
plan equality before any restore transaction directory or rollback file is
created. A caller therefore cannot use a coincident legacy checkpoint plan as a
shortcut around promotion.

Windows currently returns `UnsupportedPlatform` from revision-checkpoint
promotion and durable restore preparation before stronger restore authority is
issued. NTFS durable file write/rename is implemented, but durable
creation/promotion of the directory tree is not yet proved to the same standard.
The project therefore does not infer a complete Windows checkpoint/restore proof
from file rename semantics alone.

The manifest must never become power-loss authority while any file it names is
still only cache-resident or its directory ancestry can disappear independently.

## Durable directory and rollback namespace

`PartyQuestStableStorage::EnsureDirectoryTreeDurably` currently provides the
reviewed directory-tree promotion primitive on POSIX/Linux:

- walk each absolute path component;
- reject symlink and non-directory components;
- create missing directories one at a time;
- `fsync` the containing directory to persist the child name;
- `fsync` the child directory itself before descending;
- repeat the parent/child barriers for already-existing components, allowing a
  directory created by an earlier crash-resilient path to be promoted later.

`PartyQuestStableStorage::CopyFileDurably` provides the bounded-memory rollback
copy primitive used by Linux strong restore preparation:

- open source and destination with `O_NOFOLLOW`;
- validate exact regular-file handles;
- detect source/destination hard-link aliasing before truncation;
- stream bytes through a bounded buffer;
- `fsync` the exact destination;
- close both descriptors;
- `fsync` the destination parent so the rollback name is stable.

Windows deliberately returns `Unsupported` from both durable directory-tree and
rollback-copy primitives. `CreateDirectory` does not expose the same reviewed
write-through creation contract used by the NTFS file rename path, and this
project does not assume a generic directory `FlushFileBuffers` contract.

## Destructive restore

`PartyQuestReplicaRestoreExecutor` still enforces the legacy logical recovery
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

Linux now has a separate strong implementation of steps 1-3 only. Its ordering
is:

1. acquire the exact campaign/player workspace lease;
2. promote and rebind the exact immutable revision checkpoint;
3. prepare the restore journal from current live destinations;
4. check the existing restore resource/free-space budget;
5. durably establish transaction and rollback directories;
6. durably publish `Prepared`;
7. revalidate every live destination and checkpoint source;
8. durably copy every existing live destination into its rollback path;
9. verify exact rollback size/digest;
10. revalidate destinations and checkpoint sources again;
11. verify the complete rollback set;
12. mark and durably publish `BackupsReady`;
13. return without `MutationStarted` and without changing a live replica file.

This state remains `ResumeBeforeMutation`. Reusing the same restore id is rejected
rather than overwriting the durable transaction.

The remaining destructive `PowerLossDurable` ordering is still open:

- stage/verify checkpoint bytes under a reacquired exact workspace;
- revalidate checkpoint, rollback and live destination evidence;
- durable `MutationStarted` before any destructive live destination rename;
- each restored destination bytes, names and ancestry durable before durable
  `Restored`;
- durable `Committed` only after every restored-state barrier completes.

Rollback/recovery publication has the same requirement in reverse: restored
original bytes and namespace changes must be durable before recovery can claim
that original state has been restored.

Windows durable delete/cleanup is still unproved. Therefore no restore path may
claim that recovery authority was durably removed on Windows merely because a
normal `remove` or delete-on-close operation succeeded.

## OS primitive boundary

`PartyQuestStableStorage::FlushFile` supplies an explicit existing-file flush:

- Windows: `FlushFileBuffers` on an existing regular non-reparse file handle;
- Linux: `fsync` on an opened regular-file descriptor with `O_NOFOLLOW`.

`PartyQuestStableStorage::WriteFileDurably` supplies durable staged-file write:

- Linux: open/write `O_NOFOLLOW`, `fsync(file)`, close, then `fsync(parent)` so
  both data and a newly created staged name cross the durability barrier;
- Windows: validate an existing non-reparse parent, require NTFS, open the exact
  final node with `FILE_FLAG_WRITE_THROUGH`, validate it before truncation,
  write all bytes, `FlushFileBuffers`, then close.

`PartyQuestStableStorage::PublishFileRename` supplies same-directory publication:

- Linux: `fsync(source)`, rename, then `fsync(parent)`;
- Windows: open the exact regular non-reparse source with `DELETE` and
  `FILE_FLAG_WRITE_THROUGH`, require NTFS by handle, `FlushFileBuffers`, issue
  `SetFileInformationByHandle(FileRenameInfo)`, then apply an additional
  exact-handle flush before close.

`PartyQuestStableStorage::FlushDirectory` / `FlushParentDirectory` remain:

- Linux: `fsync` on a directory descriptor;
- Windows: `Unsupported` as a generic primitive.

`PartyQuestStableStorage::RemoveFileDurably` remains:

- Linux: validate regular final node, `unlink`, then `fsync(parent)`;
- Windows: `Unsupported` until delete/disposition plus handle-close metadata
  durability is accepted to the same standard.

## P0-H closure conditions

Do not raise `CurrentLocalGuarantee` to `PowerLossDurable` until all of the
following are true:

1. every authoritative metadata production caller uses the reviewed strong
   writer only after its parent namespace is durably established;
2. the production checkpoint path requires successful data-before-manifest
   durability promotion rather than merely exposing the promotion helper;
3. the destructive restore continuation after `BackupsReady` durably orders
   `MutationStarted`, live replacement, `Restored`, `Committed` and rollback;
4. recovery/adoption paths use the same durability contract rather than silently
   downgrading it after a crash;
5. Linux file + directory ordering is covered by deterministic failure/fault
   tests at every durable boundary;
6. Windows has a reviewed durable directory-creation/promotion contract for the
   checkpoint tree or remains explicitly unable to satisfy that production gate;
7. Windows durable delete/cleanup semantics required by restore/recovery are
   proved without administrator-only volume flushing;
8. cross-platform CI is green and the final live validation matrix records the
   exact tested SHA, filesystem and runtime assumptions.

Until then, `AllowsNativeRuntimeMutation()` must remain false and canonical
Skyrim mutation must remain disabled.
