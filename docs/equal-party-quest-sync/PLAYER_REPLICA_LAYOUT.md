# Player-scoped co-op replica foundation

This milestone adds the non-mutating identity and path foundation required before Skyrim saves can be copied or repaired.

## Stable identities

The campaign already owns a stable 128-bit `CampaignId`.

A co-op character now also has a stable 128-bit `PartyQuestPlayerProfileId`, independent of transient network `PlayerId`. Immutable profile identity metadata has deterministic encoding, checksums, atomic replacement and equivalent-backup recovery.

## Isolated path model

`PartyQuestCoopSaveLayout` plans, but does not create, this tree:

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
      metadata/
```

The planner performs no filesystem mutation. Original solo saves are intentionally outside the co-op tree.

## Runtime side-effect journal scope

The runtime apply recovery archive is format v2 and is bound to both `CampaignId` and `PlayerProfileId`.

A recovery journal from a different campaign returns `CampaignMismatch`. A recovery journal from another local co-op character returns `PlayerProfileMismatch`. This prevents one character's committed runtime transaction ids or crash-recovery barrier from being reused for another character.

The journal remains fail-closed: a valid `.tmp` can recover the newest fully written interrupted atomic replacement, while an older `.bak` is surfaced only as `BackupRecoveryRequired` and is never silently treated as current runtime truth.

## Safety boundary

No save file is copied, renamed, deleted or overwritten by these types. No canonical quest state is applied to Skyrim. The next integration stage must connect this layout to explicit co-op save/checkpoint management while preserving the invariant that solo saves remain untouched.
