# Equal-Party Persistence Durability Boundary

## Status

The production native-mutation durability requirement is `PowerLossDurable`.
The current implementation still advertises only `ProcessCrashResilient` through
`PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee`.

This document is an ordering inventory and closure checklist. It is not evidence
that P0-H is closed. `PartyQuestStableStorage` now provides reviewed staged-file
write and same-directory rename-publication primitives, and
`PartyQuestRuntimeApplyPersistence::SavePowerLossDurably` is the first metadata
writer migrated to that stronger ordering. No caller gains native mutation
authority merely because this one writer can use those primitives.

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

`PartyQuestRuntimeApplyPersistence` additionally exposes
`SavePowerLossDurably`. Its stronger path requires the destination directory to
already exist and applies this ordering:

1. encode the complete candidate journal;
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

This is one migrated writer only. The other authoritative metadata stores still
require equivalent migration and fault proof before P0-H can close.

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
2. force each staged file's bytes and staged namespace entry to stable storage;
3. publish each final data-file directory entry;
4. force the platform-specific metadata required to make those publications
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

`PartyQuestStableStorage::FlushFile` supplies an explicit existing-file flush:

- Windows: `FlushFileBuffers` on an existing regular non-reparse file handle;
- Linux: `fsync` on an opened regular-file descriptor with `O_NOFOLLOW`.

`PartyQuestStableStorage::WriteFileDurably` supplies durable staged-file write:

- Linux: open/write `O_NOFOLLOW`, `fsync(file)`, close, then `fsync(parent)` so
  both data and a newly created staged name cross the durability barrier;
- Windows: validate an existing non-reparse parent, require the filesystem to
  identify as NTFS, open/create the exact final node with
  `FILE_FLAG_WRITE_THROUGH`, validate it as regular/non-reparse before
  truncation, write all bytes, `FlushFileBuffers`, then close.

`PartyQuestStableStorage::PublishFileRename` supplies same-directory publication:

- Linux: `fsync(source)`, rename, then `fsync(parent)`;
- Windows: open the exact regular non-reparse source with `DELETE` and
  `FILE_FLAG_WRITE_THROUGH`, require NTFS by handle, `FlushFileBuffers`, issue
  `SetFileInformationByHandle(FileRenameInfo)`, then apply an additional
  exact-handle flush before close. The Windows claim is intentionally NTFS-only;
  other filesystems fail closed.

`PartyQuestStableStorage::FlushDirectory` / `FlushParentDirectory` remain:

- Linux: `fsync` on a directory descriptor;
- Windows: `Unsupported` as a generic primitive.

That Windows result is still intentional: the project does not infer a generic
POSIX-style parent-directory contract from undocumented behavior. The narrower
NTFS write-through creation/rename primitives are used where Microsoft provides
specific metadata semantics.

`PartyQuestStableStorage::RemoveFileDurably` currently:

- Linux: validates a regular final node, `unlink`, then `fsync(parent)`;
- Windows: `Unsupported` until delete/disposition plus handle-close metadata
  durability is documented and accepted to the same standard.

Therefore durable cleanup/delete is still a Windows P0-H gap even though staged
write and rename publication now have an NTFS-gated implementation.

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
6. Windows NTFS staged-write/rename assumptions and any required durable delete/
   cleanup semantics are covered without administrator-only volume flushing;
7. cross-platform CI is green and the final live validation matrix records the
   exact tested SHA, filesystem and runtime assumptions.

Until then, `AllowsNativeRuntimeMutation()` must remain false and canonical
Skyrim mutation must remain disabled.
