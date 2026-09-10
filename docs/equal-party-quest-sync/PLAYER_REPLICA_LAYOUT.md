# Player-scoped co-op replica foundation

The co-op campaign now has a filesystem-safe replica stack in addition to the earlier identity/path model. It is still not wired to Skyrim save/load hooks, so the game itself is not redirected to these files yet.

## Stable identities

The campaign owns a stable 128-bit `CampaignId`.

Each co-op character owns a stable 128-bit `PartyQuestPlayerProfileId`, independent of transient network `PlayerId`. Immutable profile identity metadata has deterministic encoding, checksums, atomic replacement and equivalent-backup recovery.

The runtime side-effect journal is bound to both identities, so one character cannot accidentally reuse another character's runtime-apply idempotency/recovery state.

## Isolated path model

`PartyQuestCoopSaveLayout` defines:

```text
CoopCampaigns/
  Campaign_<CampaignId>/
    Player_<PlayerProfileId>/
      checkpoints/
        PreJoin/
        PreMigration/
        PreRepair/
        SessionStart/
        LastKnownGood/
      saves/
      sidecars/
        party_quest_runtime_apply.bin
        external/
      metadata/
        replica_manifest.bin
```

Original solo saves remain outside the player replica tree.

## Immutable revision checkpoints

Production-oriented checkpoint publication now uses immutable revision directories such as:

```text
checkpoints/PreRepair/
  Revision_000000000000019A/
    manifest.bin
    saves/
    sidecars/external/
  Revision_000000000000019B/
    manifest.bin
    saves/
    sidecars/external/
```

`PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan`, `PartyQuestReplicaFileExecutor::ExecuteRevisionCheckpoint`, and `PartyQuestReplicaSnapshotManager::EnsureRevisionCheckpoint` all bind publication to a non-zero campaign world revision. A later checkpoint therefore gets a different destination tree rather than overwriting an older recovery point.

Legacy checkpoint-kind-root methods remain only for compatibility with earlier tests/archive tooling; new recovery work should use `RevisionCheckpoint` manifests.

## Verified file inspection and copy execution

`PartyQuestReplicaFileExecutor` performs real filesystem copies only when explicitly invoked. It remains game-independent and is not called by Skyrim runtime code.

Before publication it verifies:

- the source is a regular non-symlink file;
- the source still matches the size and deterministic content digest captured during planning;
- import sources are outside the co-op player tree;
- checkpoint sources are inside the player tree but outside `checkpoints/`;
- destinations stay inside the expected `saves/`, `sidecars/external/`, or selected checkpoint revision subtree;
- final destinations do not already exist;
- existing path resolution does not redirect the destination outside the effective player root.

The complete file set is first copied to temporary sibling files and verified. Only after all temporary files match are they renamed into their final create-only destinations. Normal in-process publication failures roll back files created by that call. Source files are never deleted or modified.

The 64-bit file digest is used as a local transport-integrity checksum, not as a cryptographic trust primitive. Mod compatibility and hostile-client validation use separate fingerprint/authority rules.

## Durable completion manifests

A process can still die between per-file renames, so the presence of copied files alone is not treated as proof that a multi-file replica/checkpoint completed.

`PartyQuestReplicaManifestStore` writes a checksum-protected completion manifest only after the final file set verifies. A manifest contains:

- `CampaignId`;
- `PlayerProfileId`;
- imported-replica, legacy checkpoint, or immutable revision-checkpoint type;
- checkpoint kind;
- campaign world revision captured for the snapshot;
- relative file paths;
- file kinds;
- expected sizes and digests.

For `RevisionCheckpoint`, verification derives the exact checkpoint root from `CheckpointKind + CampaignWorldRevision`, so revision 700 cannot accidentally validate bytes from revision 701.

Future use must reload the manifest and verify the published bytes again. A valid `.tmp` is preferred after an interrupted manifest replacement. An older `.bak` is surfaced as `BackupRecoveryRequired` rather than silently accepted as current truth.

## Snapshot transaction manager

`PartyQuestReplicaSnapshotManager` combines copy execution, final byte verification and durable manifest publication into a single filesystem transaction boundary.

It can also recover one specific crash window safely: if all final files were published and verified but the process died before writing the manifest, a later call may adopt that exact complete byte set and finish the manifest. Partial or conflicting orphaned copies are not adopted.

A valid existing snapshot is never overwritten. Different content behind an existing valid manifest is treated as a conflict. Distinct revision checkpoints may coexist and validate independently; corruption of one revision does not invalidate another.

## Restore planning

`PartyQuestReplicaRestorePlanner` builds a non-executing restore plan from a verified legacy or revision checkpoint manifest.

For a revision checkpoint, its source root is derived from the manifest world revision. It maps files back only to the current co-op replica's:

- `saves/`;
- `sidecars/external/`.

It never targets solo-save paths, checkpoint paths, metadata, or `party_quest_runtime_apply.bin`. Changed/corrupt checkpoint bytes block restore planning before an overwrite can be attempted.

The destructive restore executor is intentionally not implemented yet. Before that is allowed, restore itself needs a crash-resumable journal/rollback protocol so a process failure halfway through replacement cannot leave a replica ambiguously half-restored.

## Runtime side-effect journal scope

The runtime apply recovery archive is format v2 and is bound to both `CampaignId` and `PlayerProfileId`.

A recovery journal from a different campaign returns `CampaignMismatch`. A recovery journal from another local co-op character returns `PlayerProfileMismatch`.

The journal remains fail-closed: a valid `.tmp` can recover the newest fully written interrupted atomic replacement, while an older `.bak` is surfaced only as `BackupRecoveryRequired` and is never silently treated as current runtime truth.

## Current safety boundary

Filesystem copy execution exists now, but it operates only when explicitly invoked by tests/library callers. The live Skyrim client still does not:

- redirect Skyrim save/load to the co-op replica;
- automatically import a solo `.ess`/`.skse`/external sidecar set;
- overwrite/restore live replica files from a checkpoint;
- intercept manual/auto/quick saves;
- apply canonical quest state to Skyrim.

The next filesystem-critical step is a crash-resumable destructive restore journal/executor, followed by concrete client save interception/checkpoint hooks. Canonical quest mutation remains disabled until those protections and the Papyrus/world-target gates are live and validated.