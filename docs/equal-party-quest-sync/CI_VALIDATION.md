# Equal-party quest PoC validation

The PoC remains diagnostic-only. Canonical repair state is not applied back into Skyrim quests or save files.

## Automated validation

`TPTests` exercises the game-independent canonical state, persistence, protocol, repair, reconnect and divergence layers.

Current coverage includes:

- deterministic `QuestSnapshot` canonicalization and digests;
- world/per-quest revision assignment;
- idempotent transaction IDs and conflicting transaction detection;
- checkpoint/journal persistence, replay validation, checksums, atomic replacement and `.bak` recovery;
- durable save-before-publish behavior and failed-write rollback;
- stable persisted `CampaignId` independent of transient Party IDs;
- replica reports, deterministic repair plans and repair ACK verification;
- reconnect-safe client protocol ID allocation;
- retained protocol replica across transient `PlayerId` rebinding;
- reset on genuine `CampaignId` change;
- campaign verification gating and per-quest submission coalescing;
- two simulated connected clients converging after independent divergence;
- same-revision payload divergence detected by digest;
- missed canonical update detected by quest revision;
- client-only quest state preserved locally and never admitted into canonical campaign state by repair;
- repair ACK correlation isolated per authenticated client session;
- repair divergence summaries split into missing, revision mismatch, digest mismatch and client-only counts;
- diagnostic quest admission classification for user-facing gameplay candidates versus hidden service candidates.

## Diagnostic quest classification

The live collector labels each observed quest with one of:

- `shared-candidate` — ordinary gameplay quest types, plus user-facing `None`/`Miscellaneous` quests;
- `service-candidate` — hidden `None`/`Miscellaneous` quests that may be controllers, trackers or helper quests;
- `local-only` — currently used for quests without stages.

The classifier also logs a reason code such as `gameplay-type`, `user-facing-misc`, `hidden-misc`, or `hidden-untyped`.

This classification is intentionally observational in the current milestone. `service-candidate` quests are still allowed through the diagnostic canonical protocol so real two-client evidence can be collected before an admission filter becomes authoritative. Classification metadata is not part of `QuestSnapshot`, its digest, the wire format, or persisted campaign state.

## Live validation completed

A restart/reconnect campaign test confirmed:

- stable `CampaignId=A0C27D9E9A1E822D3EE502D620B25F94`;
- canonical persistence restoration from world revision 182 with 33 quests and 182 journal entries;
- retained client replica for the same campaign across server restart;
- reconnect report at the retained world revision with no unnecessary full repair;
- first post-restart transaction accepted immediately;
- removal of the previous historical transaction-ID conflict storm;
- continued accepted canonical transactions after restart.

## Current CI validation

The divergence/service-classification code milestone was validated at commit `fe2f1570a17b37f3445f9826f8b9b4445f61ef6a`:

- Build Windows: passed;
- Build Linux: passed;
- Equal party PoC diagnostics: passed;
- `TPTests`: built and executed successfully.

## Next live validation

Use two connected game clients with the same build and campaign:

1. converge both protocol replicas on the same campaign;
2. allow client A to advance a visible gameplay quest and confirm both replicas receive the canonical broadcast;
3. disconnect client B, advance the campaign from A, then reconnect B and confirm deterministic repair;
4. collect `syncClass`/`syncReason` evidence for high-churn service quests on both clients;
5. compare hidden service-candidate behavior before turning classification into an authoritative admission policy.

No canonical `SetStage`, alias restoration, inventory mutation or save-file mutation should be enabled until this two-client validation is reviewed.
