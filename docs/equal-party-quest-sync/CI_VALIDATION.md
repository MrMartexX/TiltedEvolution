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
- diagnostic quest admission classification for user-facing gameplay candidates versus hidden service candidates;
- automatic one-PC shadow-peer scenario that performs initial convergence, applies one baseline update, deliberately disconnects for the next accepted update, reconnects and repairs the missed update, then injects a same-revision digest divergence and repairs it.

## Diagnostic quest classification

The live collector labels each observed quest with one of:

- `shared-candidate` — ordinary gameplay quest types, plus user-facing `None`/`Miscellaneous` quests;
- `service-candidate` — hidden `None`/`Miscellaneous` quests that may be controllers, trackers or helper quests;
- `local-only` — currently used for quests without stages.

The classifier also logs a reason code such as `gameplay-type`, `user-facing-misc`, `hidden-misc`, or `hidden-untyped`.

This classification is intentionally observational in the current milestone. `service-candidate` quests are still allowed through the diagnostic canonical protocol so real evidence can be collected before an admission filter becomes authoritative. Classification metadata is not part of `QuestSnapshot`, its digest, the wire format, or persisted campaign state.

## One-PC shadow peer live mode

For a user with one Skyrim installation and one PC, the server can create a synthetic second protocol replica without launching another Skyrim process.

Enable:

```ini
[Gameplay]
bEnablePartyQuestProtocolDiagnostics=true
bEnablePartyQuestStatePersistence=true
bEnablePartyQuestShadowPeerTest=true
sPartyQuestStatePath=state/party_quest_campaign.bin
```

The shadow peer is server-local and never mutates Skyrim. It uses synthetic client id `0xFFFFFFFE` only inside `PartyQuestProtocolCoordinator`.

The live sequence is automatic:

1. synchronize the shadow replica to the current persisted campaign;
2. consume the first accepted canonical update as a baseline;
3. disconnect the shadow peer before the next update;
4. deliberately miss the second accepted canonical update;
5. reconnect and verify report → repair → ACK convergence;
6. alter one local snapshot payload while keeping its quest/world revision unchanged;
7. verify that the next report detects `DigestMismatch` and repairs it;
8. disconnect the synthetic peer and emit `PartyQuestShadowPeer TEST PASS`.

The server emits `TEST START`, `STEP 1 PASS`, and a final `TEST PASS`/`TEST FAIL` line, so the user does not need to manually operate a second client or deliberately time disconnects.

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

The one-PC shadow-peer code milestone is commit `31d096c699b92be49ab9548267e0321d78ed92f2`.

At that commit:

- Build Windows: passed;
- Equal party PoC Windows runtime build: passed;
- `TPTests`: built and executed successfully, including the shadow-peer missed-update + digest-divergence scenario;
- the full Linux build is tracked by its normal workflow separately.

This documentation commit does not alter the validated code.

## Next live validation

Use one real Skyrim client plus the server-local shadow peer:

1. join the existing persisted campaign with the same client/server build;
2. create/join the Party normally;
3. allow at least two accepted quest transactions (ordinary play is sufficient);
4. confirm `PartyQuestShadowPeer TEST PASS` in the server log;
5. continue playing briefly to collect `syncClass`/`syncReason` evidence for service-quest classification;
6. review the server/client logs before turning service classification into an authoritative admission filter.

No canonical `SetStage`, alias restoration, inventory mutation or save-file mutation should be enabled until this validation is reviewed.
