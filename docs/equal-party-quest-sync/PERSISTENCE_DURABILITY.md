# Equal-Party Persistence Durability Boundary

## Status

The production native-mutation durability requirement is `PowerLossDurable`.
The current implementation still advertises only `ProcessCrashResilient` through
`PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee`.

This document is an ordering inventory and closure checklist. It is not evidence
that P0-H is closed. `PartyQuestStableStorage` provides reviewed staged-file
write and same-directory rename-publication primitives. Three authoritative
metadata stores now expose stronger publication paths:

- `PartyQuestRuntimeApplyPersistence::SavePowerLossDurably`;
- `PartyQuestReplicaManifestStore::SavePowerLossDurably`;
- `PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably`.

The restore journal also exposes `LoadPowerLossDurably` so recovery of a newer
valid `.tmp` does not silently downgrade to an ordinary rename. No caller gains
native mutation authority merely because these individual paths exist.

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

The three migrated stronger writers use the same core ordering:

1. encode the complete candidate state;
2. durably create/truncate and write `.tmp`;
3. re-read and decode `.tmp` and verify exact state equality;
4. if a primary exists, durably rename primary to `.bak`, replacing an older
   backup when necessary;
5. durably rename the verified `.tmp` to the primary name;
6. never perform an unproven rollback rename after a failed durable boundary.

After step 4, failure deliberately preserves the old durable state as `.bak`
and the new durable candidate as `.tmp`. After step 5, a later reported failure
must treat the new primary as potentially/durably published rather than pretend
publication did not occur.

`PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably` is the
recovery partner for the strong restore-journal writer:

- a valid primary is returned directly;
- a valid `.tmp` is promoted only with `PublishFileRename` and only when the
  primary is absent;
- a present invalid primary plus valid `.tmp` is conflicting evidence and is
  preserved fail-closed;
- an older valid `.bak` remains `BackupRecoveryRequired`, never normal current
  truth.

Still not migrated:

- `PartyQuestCampaignPersistence`;
- `PartyQuestPlayerProfilePersistence`;
- `PartyQuestStatePersistence`.

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

Windows currently returns `UnsupportedPlatform` from revision-checkpoint
promotion before issuing stronger authority. NTFS durable file write/rename is
implemented, but durable creation/promotion of the directory tree is not yet
proved to the same standard. The project therefore does not infer a complete
Windows checkpoint-durability proof from file rename semantics alone.

The manifest must never become power-loss authority while any file it names is
still only cache-resident or its directory ancestry can disappear independently.

## Durable directory namespace

`PartyQuestStableStorage::EnsureDirectoryTreeDurably` currently provides the
reviewed directory-tree promotion primitive on POSIX/Linux:

- walk each absolute path component;
- reject symlink and non-directory components;
- create missing directories one at a time;
- `fsync` the containing directory to persist the child name;
- `fsync` the child directory itself before descending;
- repeat the parent/child barriers for already-existing components, allowing a
  directory created by an earlier crash-resilient path to be promoted later.

Windows deliberately returns `Unsupported`. `CreateDirectory` does not expose
the same reviewed write-through creation contract used by the NTFS file rename
path, and this project does not assume a generic directory `FlushFileBuffers`
contract.

## Destructive restore

`PartyQuestReplicaRestoreExecutor` still enforces this logical recovery sequence:

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

The restore journal writer can now durably publish a phase, but the executor has
not yet been migrated to prove the filesystem prerequisites before each phase.
For `PowerLossDurable`, the required ordering remains:

- rollback backup bytes, names and directory ancestry durable before durable
  `BackupsReady`;
- durable `MutationStarted` before destructive destination replacement;
- restored destination bytes, names and ancestry durable before durable
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

1. every authoritative metadata store uses a reviewed stable-storage publication
   protocol;
2. the production checkpoint path requires successful data-before-manifest
   durability promotion rather than merely exposing the promotion helper;
3. restore rollback backups, destructive replacements and every journal phase
   transition obey the ordering above;
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
