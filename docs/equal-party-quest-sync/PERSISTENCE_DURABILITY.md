# Equal-Party Persistence Durability Boundary

## Status

The production native-mutation durability requirement is `PowerLossDurable`.
The current implementation still advertises only `ProcessCrashResilient` through
`PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee`.

This document is an ordering inventory and closure checklist. It is not evidence
that P0-H is closed. `PartyQuestStableStorage` provides reviewed staged-file,
copy, rename-publication and POSIX namespace-removal primitives.

All six authoritative metadata stores expose a stronger publication path:

- `PartyQuestCampaignPersistence::SavePowerLossDurably`;
- `PartyQuestPlayerProfilePersistence::SavePowerLossDurably`;
- `PartyQuestStatePersistence::SavePowerLossDurably`;
- `PartyQuestRuntimeApplyPersistence::SavePowerLossDurably`;
- `PartyQuestReplicaManifestStore::SavePowerLossDurably`;
- `PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably`.

Linux additionally has a complete isolated strong replica-restore filesystem
pipeline split into two surfaces:

- `PartyQuestReplicaDurableRestorePreparation::Prepare` establishes the promoted
  checkpoint, durable `Prepared`, exact durable rollback evidence and durable
  `BackupsReady` without touching a live replica destination;
- `PartyQuestReplicaDurableRestoreExecutor::Continue` reacquires and revalidates
  that exact evidence, durably stages the checkpoint, publishes durable
  `MutationStarted`, durably replaces and verifies live replica files, publishes
  durable `Restored` and `Committed`, then compacts terminal transaction evidence.

`PartyQuestReplicaDurableRestoreExecutor::Recover` is the recovery partner for
post-barrier failures. It does not cross a new mutation barrier from
`Prepared`/`BackupsReady`; `MutationStarted` is rolled back to exact original
replica bytes, `Restored` is verified before commit, and `Committed` is verified
before cleanup is resumed.

This remains a filesystem proof surface, not production native-mutation
activation. Existing runtime/server callers are not automatically converted
merely because the stronger functions exist. The server canonical-state/campaign
bootstrap and other production persistence paths still contain legacy
crash-resilient writers. The new strong restore executor is also not wired as a
runtime mutation authorization source.

The restore journal exposes `LoadPowerLossDurably` so recovery of a newer valid
`.tmp` does not silently downgrade to an ordinary rename. Player-profile
persistence also exposes `LoadPowerLossDurably` so a first-publication durable
`.tmp` preserves the immutable ProfileId across a crash without manufacturing
lineage authority. No caller gains Skyrim/Papyrus/world mutation authority merely
because any of these individual paths exist.

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

Having strong writer APIs and an isolated strong restore executor is not
end-to-end production activation. Production P0-H still requires each owner to
prove its surrounding directory namespace before choosing the strong path.
Current canonical server persistence still uses legacy
`PartyQuestStatePersistence::SaveAtomically` and
`PartyQuestCampaignPersistence::SaveAtomically`; switching those calls without a
cross-platform parent-directory contract would incorrectly make Windows startup
fail or overclaim durability.

Likewise, profile/runtime/manifest/journal strong APIs must only become production
requirements when their owning co-op directory tree has a compatible durable
namespace proof.

The Linux strong restore classes are intentionally separate from legacy
`PartyQuestReplicaRestoreExecutor::Execute/Recover`. A legacy caller cannot treat
a strong `BackupsReady` journal as permission to continue through ordinary
`SaveAtomically` phase transitions. Conversely,
`PartyQuestReplicaDurableRestoreExecutor::Recover` returns
`ResumeBeforeMutation` for `Prepared` or `BackupsReady`; recovery never creates a
new durable `MutationStarted` barrier on its own.

## Replica/checkpoint data files

`PartyQuestReplicaFileExecutor` still performs the process-crash copy protocol:
stage, verify, rename into the final destination, then verify the final file.
That executor is intentionally not relabelled as power-loss durable.

Instead, immutable revision checkpoints have an explicit promotion path:
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
created. `PartyQuestReplicaDurableRestoreExecutor::Continue` repeats promotion
and plan binding before staging or publishing `MutationStarted`, so a stale
`BackupsReady` transaction cannot bypass current checkpoint authority.

Windows currently returns `UnsupportedPlatform` from revision-checkpoint
promotion, durable restore preparation and the strong destructive executor before
strong restore mutation is issued. NTFS durable file write/rename is implemented,
but durable creation/promotion/removal of the directory tree is not yet proved to
the same standard. The project therefore does not infer a complete Windows
checkpoint/restore proof from file rename semantics alone.

The manifest must never become power-loss authority while any file it names is
still only cache-resident or its directory ancestry can disappear independently.

## Durable directory, rollback and cleanup namespace

`PartyQuestStableStorage::EnsureDirectoryTreeDurably` provides the reviewed
POSIX/Linux directory-tree promotion primitive:

- walk each absolute path component;
- reject symlink and non-directory components;
- create missing directories one at a time;
- `fsync` the containing directory to persist the child name;
- `fsync` the child directory itself before descending;
- repeat parent/child barriers for already-existing components, allowing a
  directory created by an earlier crash-resilient path to be promoted later.

`PartyQuestStableStorage::CopyFileDurably` provides the bounded-memory rollback
and staging copy primitive used by the Linux strong restore path:

- open source and destination with `O_NOFOLLOW`;
- validate exact regular-file handles;
- detect source/destination hard-link aliasing before truncation;
- stream bytes through a bounded buffer;
- `fsync` the exact destination;
- close both descriptors;
- `fsync` the destination parent so the published name is stable.

`PartyQuestStableStorage::RemoveFileDurably` supplies POSIX durable file removal:
validate a regular final node, `unlink`, then `fsync(parent)`.

`PartyQuestStableStorage::RemoveEmptyDirectoryDurably` supplies narrowly scoped
POSIX durable empty-directory removal: validate a directory final node, `rmdir`,
then `fsync(parent)`. It is not recursive deletion authority; callers must prove
all confinement and emptiness preconditions.

Windows deliberately returns `Unsupported` from durable directory-tree, durable
copy, file removal and empty-directory removal primitives. `CreateDirectory` and
delete/disposition semantics are not assumed to provide the same reviewed
stable namespace contract merely because NTFS write-through file rename exists.

## Strong destructive restore

The legacy `PartyQuestReplicaRestoreExecutor` remains a separate
process-crash-resilient path. The Linux strong path now covers the complete
filesystem phase sequence from an already verified checkpoint to terminal
commit.

### Strong pre-mutation preparation

`PartyQuestReplicaDurableRestorePreparation::Prepare` orders:

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

### Strong destructive continuation

`PartyQuestReplicaDurableRestoreExecutor::Continue` accepts only exact
`BackupsReady` state and orders:

1. reacquire the exact campaign/player workspace lease;
2. load the journal with `LoadPowerLossDurably` and revalidate its exact confined
   layout and identity;
3. re-promote the revision checkpoint and rebind the journal's operation set to
   the exact current promoted manifest;
4. re-flush/reverify all rollback evidence;
5. durably create exact same-directory forward staging files from checkpoint
   sources and verify their size/digest;
6. revalidate every checkpoint source and every live destination against the
   original `BackupsReady` observations;
7. mark and durably publish `MutationStarted`;
8. only after that barrier, durably rename each staged file into its exact live
   co-op replica destination and verify/flush the final file and containing
   directory;
9. durably reverify the complete restored target set;
10. mark and durably publish `Restored`;
11. mark and durably publish `Committed`;
12. compact terminal evidence only after `Committed` is durable and final targets
    still verify.

The implementation intentionally does not move old live destinations into an
ad-hoc sibling backup after the barrier: the exact durable rollback set was
already established before `BackupsReady`, and publication uses same-directory
atomic replacement. A post-barrier failure therefore recovers from the durable
rollback set rather than depending on another transient generation.

### Recovery after the mutation barrier

`PartyQuestReplicaDurableRestoreExecutor::Recover` obeys the persisted phase:

- `Prepared` / `BackupsReady`: return `ResumeBeforeMutation`; do not create
  `MutationStarted`;
- `MutationStarted`: restore original destinations in reverse operation order;
  destinations that did not originally exist are durably removed, while existing
  destinations are recreated from exact durable rollback bytes via durable
  sibling staging + atomic replacement; verify the complete original state;
- `Restored`: durably verify all restored destinations, then publish `Committed`;
  if restored verification fails, take the exact rollback path;
- `Committed`: durably verify final restored destinations and resume only terminal
  compaction, never rollback a successfully committed transaction.

Fault tests cover cuts immediately after durable `MutationStarted`, after one
partial destination publication, after durable `Restored`, and after durable
`Committed`.

### Terminal transaction identity and compaction

A successful committed transaction is not fully deleted. Doing so would make the
same `RestoreId` reusable after cleanup and weaken transaction identity.

After durable `Committed`, compaction may durably remove:

- forward/rollback staging files;
- rollback backup files;
- obsolete journal `.tmp` and `.bak` files;
- now-empty rollback subdirectories.

The primary `Committed` journal and transaction directory remain as a compact
durable tombstone. This keeps the completed restore id occupied and lets later
`Recover` reverify final postconditions without reconstructing history from
filenames or timestamps.

A rolled-back `MutationStarted` transaction is intentionally more conservative.
The current durable schema has no terminal `RolledBack` phase, so recovery keeps
the `MutationStarted` primary journal and rollback evidence after exact original
state is restored. Repeated recovery is idempotent. Adding a durable rolled-back
terminal state, and only then compacting that recovery evidence, remains open
P0-H work.

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

`PartyQuestStableStorage::RemoveFileDurably` and
`RemoveEmptyDirectoryDurably` remain:

- Linux: `unlink`/`rmdir` the validated final node, then `fsync(parent)`;
- Windows: `Unsupported` until file/directory deletion and metadata publication
  semantics are accepted to the same standard.

## P0-H closure conditions

Do not raise `CurrentLocalGuarantee` to `PowerLossDurable` until all of the
following are true:

1. every authoritative metadata production caller uses the reviewed strong
   writer only after its parent namespace is durably established;
2. the production checkpoint path requires successful data-before-manifest
   durability promotion rather than merely exposing the promotion helper;
3. the production restore path requires the reviewed strong preparation and
   destructive continuation instead of the legacy crash-resilient executor;
4. rolled-back post-barrier transactions have an explicit durable terminal state
   if their retained rollback evidence is to be compacted safely;
5. recovery/adoption paths use the same durability contract rather than silently
   downgrading it after a crash;
6. Linux file + directory ordering is covered by deterministic boundary faults
   and by filesystem/device power-loss validation under documented assumptions;
7. Windows has a reviewed durable directory-creation/promotion/removal contract
   for the checkpoint/restore tree or remains explicitly unable to satisfy the
   production gate;
8. Windows durable delete/cleanup semantics required by restore/recovery are
   proved without administrator-only volume flushing;
9. cross-platform CI is green and the final live validation matrix records the
   exact tested SHA, filesystem and runtime assumptions.

Until then, `AllowsNativeRuntimeMutation()` must remain false and canonical
Skyrim mutation must remain disabled.
