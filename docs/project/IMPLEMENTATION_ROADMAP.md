# Implementation roadmap

Status: normative dependency order

The roadmap distinguishes accepted implementation, local-only work, planned
work and evidence. See [CURRENT_STATUS.md](CURRENT_STATUS.md) for exact heads
and live CI results. A later step may be researched early, but it may not be
integrated ahead of an unmet safety dependency.

## Completion language

- **Accepted**: integrated into the current baseline and passed its required
  automated/live gates.
- **Local**: implementation exists but is not accepted.
- **Planned**: specification exists; no accepted implementation.
- **Evidence**: an observation or root cause, not a fix.

## Part I — Close equal-party quest-sync P0

### 0. Maintain a clean integration chain

Status: required for every task.

- Start each task from the latest accepted integration HEAD.
- Never merge a stack merely because all commits are local and clean.
- Preserve unrelated/user work in separate branches or worktrees.
- For every code-changing slice record root cause, invariant, minimal fix,
  regression test, commit, all four CI jobs and exact Linux/Windows TPTests
  counts.
- Continue read-only audit while CI runs, but do not stack an independent fix on
  an unaccepted SHA.

### 1–6. Accepted safety foundations

Status: accepted through the current integration baseline.

1. Lifecycle coverage for LoadGame, NewGame and Main Menu.
2. Production runtime bootstrap bound to campaign/player/save lineage.
3. Exact 1.6.1170 Papyrus/quiescence observer.
4. Offline compatibility analyzer and cached fingerprints.
5. Reviewed compatibility admission and transition-risk classification.
6. Pre/postcondition verification envelope with atomic result authority.

These areas must still be regression-tested by later tasks. They are not to be
redesigned without a demonstrated defect.

### 7. Prove the Skyrim/SKSE async save contract

Status: local implementation; acceptance incomplete.

Required implementation/proof:

1. Bind campaign, profile, generation, transaction, revision, capture epoch,
   attempt nonce and save name to one worker-owned request.
2. Resolve the isolated path inside the matching save worker, not around
   request admission.
3. Observe separate terminal results for `.ess` and `.skse`.
4. Propagate create/write/serialization/flush/close failure.
5. Reject overlap, timeout, mismatched completion, stale generation and
   cancellation.
6. Restore process-global path state before terminal notification.
7. Gate the extension on exact Skyrim/SKSE/Address Library/bridge identity.
8. Live-test ordinary manual/autosaves concurrently and prove they remain in
   their original directory.
9. Exercise write failures through bounded fault injection; do not fill the
   user's disk.

SetStage and native canonical mutation remain disabled.

### 8. Publish a PowerLossDurable PreRepair checkpoint

Status: local implementation; blocked on accepted Task 07 and Windows proof.

Required implementation/proof:

1. Accept only both successfully completed artifacts from the exact Task 07
   capability.
2. Verify identity, regular-file type, non-empty content, size and digest.
3. Write immutable checkpoint data, flush it, then publish a commit marker.
4. Grant no mutation capability before durable publication.
5. Confine all paths; reject traversal, symlink/reparse substitution and
   cross-campaign/profile data.
6. Recover deterministically from interruption at every write/flush/publication
   boundary.
7. Prove the actual Windows contract. If directory/rename/delete durability is
   not sufficient, use a pre-created append-only checkpoint container with
   checksummed records and a flushed commit footer instead of pretending POSIX
   semantics apply.

Linux-only durability is insufficient for a Windows Skyrim production gate.

### Supporting slice A. Integrate observational diagnostics

Status: local implementation and live evidence; not accepted.

After Task 08 is accepted, re-slice diagnostics onto the current baseline:

- read-only event recorder;
- dialogue/container/barter/death/save-state observations;
- inventory/equipment causal identities;
- bounded buffering, log rotation and offline analyzer;
- disabled-by-default policy and hot-path performance budget;
- unit tests showing diagnostics cannot mutate or authorize gameplay.

The analyzer may distinguish evidence consistent with save state versus network
state; it cannot certify that an `.ess` is globally healthy.

### Supporting slice B. Remove the proven naked-NPC item duplication source

Status: root cause proven; superseded implementation direction identified.

- Port and harden official upstream's server-granted ownership epoch model and
  former-owner rejection as a dedicated slice.
- Remove automatic item-creating `ResetInventory` repair; do not retain a timer
  as the authority model.
- If actor/server identity, current ownership grant or ledger is missing,
  quarantine/resync and fail closed.
- Test conservation of item quantities and stale-epoch rejection under repeated
  steal/equip/resync, cell-transition and reconnect events.

Full transactional pickpocket belongs to the later inventory/interaction
platform, but the known creation loop must not survive into live P0 testing.

See the dated [upstream ecosystem review](../research/UPSTREAM_ECOSYSTEM_REVIEW_2026-09-13.md)
for the exact source range and gaps that require local tests.

### Supporting slice C. Reconcile upstream safely

Status: research complete; implementation planned.

- Never wholesale-merge official `dev` into the P0 branch.
- Compare and selectively port the exact-runtime/SKSE delta first.
- Port the merged actor ownership epoch stack and follow-up fixes before broad
  live gameplay acceptance.
- Evaluate object lifecycle, jail-container, leveled-NPC and non-owner dialogue
  changes one at a time; preserve session/generation fencing and fail closed.
- Treat the old quest/scene PRs as test evidence, not permission to bypass the
  server-authoritative revision, checkpoint, verification or recovery model.
- Record exact external SHA, license, wire/ABI impact and missing live proof for
  every intake slice.

### 9. Complete deterministic crash and restart recovery

Status: planned.

Use one persisted state machine:

```text
Prepared -> CheckpointCommitted -> MutationAttempted
         -> VerifiedCommitted
         -> RecoveryRequired -> Restored -> Retired
```

- Bind every state to exact campaign/profile/transaction/revision/checkpoint.
- A crash before mutation may retire safely; a crash after possible mutation
  blocks new work until recovery.
- Select only the exact checkpoint recorded by the transaction.
- Make repeated recovery idempotent and bounded by persistent attempt/tombstone
  state.
- Clear the runtime barrier only after restored bytes and identities are
  reverified.
- Reject corrupt, legacy-domain or ambiguous journals without touching live
  replica data.

### 10. Connect the full production pipeline in strict dry-run mode

Status: planned.

Connect:

```text
canonical intent -> inbox -> runtime owner -> deferred queue
-> readiness evidence -> consume-time revalidation
-> compatibility -> quiescence -> checkpoint/verification plan
-> explicit dry-run result
```

- No SetStage or production native mutation.
- No plugin/script fingerprint scan on the game thread; use the accepted cache.
- Revalidate after readiness and under the same generation/lifecycle guard used
  by consumption.
- Give every reject/defer result a deterministic diagnostic reason.
- Live-test repeated updates, Dragonsreach, location changes, LoadGame and
  reconnect without the previous 10–50 second stalls.

### 11. Classify divergent local saves

Status: planned.

Classify observed state as exact, semantically equivalent, one reviewed edge
behind, ahead, divergent, missing, incompatible environment, unstable or
recovery-required.

- Compare the complete verification envelope, not only numeric stage.
- Permit automatic repair only along an explicit reviewed graph edge.
- Reject branch ambiguity, skipped irreversible stages, newer client state and
  incompatible script/plugin evidence.
- Test campaign/FormID ABA, same-stage/different-fragment state, revision gaps,
  LoadGame during reconciliation and repeated canonical snapshots.

### 12. Publish the first reviewed quest profiles

Status: planned.

- Extract stages, objectives, fragments, aliases, scenes, quest objects,
  scripts and irreversible side effects offline.
- Generate candidate transition graphs automatically.
- Reuse reviewed templates for demonstrably equivalent simple transitions.
- Require manual evidence for dangerous or adapter-specific edges.
- Publish a manifest atomically for exact runtime/plugin/script/native-adapter
  fingerprints.
- Start with a deliberately small linear set without aliases, quest-object
  mutation, inventory mutation, active scenes or generic world effects.
- Leave unknown quests observation-only.

This is how support scales to hundreds of quests without declaring them all
equivalent or hand-writing every profile from nothing.

### 13. Enable one narrow mutation adapter and validate two clients

Status: planned; forbidden until Tasks 01–12 are accepted.

- Add one quest-specific SetStage adapter behind a shipping-default-off flag.
- Require the complete identity, compatibility, lifecycle, quiescence,
  checkpoint and verification envelope.
- Persist `MutationAttempted` before the native call.
- Hold the generation/lifecycle guard through mutation and observation.
- Verify stage, objectives, aliases, Papyrus stability and absence of forbidden
  side effects.
- Route ambiguity to recovery, never optimistic commit.
- Test two clients with equal and divergent saves, disconnect/reconnect,
  LoadGame, duplicate/replay, out-of-order revisions, missing references,
  incompatible mods and host/leader changes.

Do not expand this adapter to aliases, inventory or generic world mutation.

### 14. Execute the live failure/recovery matrix

Status: planned.

On the exact candidate artifact, test interruption:

- before and after checkpoint publication;
- before and after possible mutation;
- after verification but before commit;
- during disconnect, reconnect, LoadGame, NewGame, Main Menu and party/campaign
  transitions;
- with late SKSE/Papyrus callbacks;
- with missing/corrupt sidecars or journal;
- with generation change and repeated recovery;
- at documented physical power-loss boundaries when required.

Preserve artifact hashes, exact runtime identities, server/client logs and
reproduction procedures.

### 15. Perform an independent adversarial audit

Status: planned.

- Reconstruct the real production path from protocol input to commit/recovery.
- Search for alternate mutation paths, stale authority, ABA, TOCTOU,
  reentrancy, pointer lifetime and shutdown escapes.
- Reproduce the critical automated and live matrix.
- Verify exact CI steps and counts on one production candidate SHA.
- Confirm shipping configuration, not only a test flag, enables only reviewed
  profiles.

Activation is a separate minimal commit followed by full CI and live smoke.
Any unresolved required gate means `P0 NOT CLOSED`.

## Part II — Shared multiplayer foundation after P0

### 16. Generalize identities, transactions and authority leases

Status: planned.

- Stable player, actor, object, container, item-stack and operation identities.
- Campaign/session/generation binding and expected revisions.
- Idempotency/replay records.
- Short-lived AI and interaction leases with revoke-before-reassign semantics.
- Versioned snapshots, ordered deltas and schema migration.
- Modular server persistence for quests, actors, inventories, containers,
  world-state, transactions and checkpoints.

This shared layer must precede separate NPC, inventory and follower ownership
systems so they do not create incompatible authority models.

### 17. Harden networking, players and reconnect

Status: planned improvement of existing STR behavior.

- Capability/protocol handshake and incompatible-version rejection.
- Stable resume identity and canonical snapshot on reconnect.
- Ordered acknowledgements, bounded replay windows and stale-session rejection.
- Backpressure, queue limits and rate limiting.
- Correct cell/world/teleport transitions and better interpolation.
- Prevent duplicate player entities and old packet authority after reconnect.

### 17A. Publish a stable first-party add-on interface

Status: planned after the core capability/session model is stable.

- Named, versioned client/server channels with authenticated sender identity.
- Explicit capability negotiation, payload quotas, backpressure and permissions.
- Public mapping from stable STR player identity to a local proxy FormID; never
  expose cached raw actor pointers.
- Transport callbacks may only enqueue bounded data; Skyrim lookup and mutation
  are scheduled on the game thread under lifecycle/generation validation.
- Disconnect, reconnect, LoadGame and proxy replacement emit deterministic
  mapping invalidation.
- Presentation-only consumers remain separated from canonical gameplay state.

This replaces the ecosystem's need to tunnel add-on payloads through chat and
hook private receive internals.

### 18. Make inventory, equipment and transfers transactional

Status: planned.

- Server revisioned inventory/equipment ledger.
- Atomic source-to-target transfer with item/stack identity and expected
  revisions.
- Idempotent player trade, follower transfer, loot, drop/pickup and
  equip/unequip.
- Two-party trade escrow and confirmation.
- Explicit unique/quest-item policy.
- Snapshot reconciliation after reconnect without local reconstruction.

### 19. Make containers and world loot canonical

Status: planned.

- Accept one actual leveled-loot result as canonical; synchronize the result,
  not RNG internals.
- Revisioned container snapshots and atomic take/put operations.
- Stable identity for important item stacks and one-time world pickups.
- Server-defined respawn/reset epochs.
- No permanent tracking requirement for decorative item transforms unless the
  object is promoted to authoritative world state.

### 20. Introduce a unified interaction coordinator

Status: planned.

- Explicit sessions for dialogue, barter, transfer and pickpocket.
- Actor/player/type/revision/distance/LOS/timeout identity.
- Cancel on combat, damage, quest scene, conflicting player interaction,
  disconnect or lease expiry.
- During pickpocket, hold authoritative NPC movement/rotation for all clients;
  a conflicting interaction cancels theft.
- Track the local menu/input-capture stack so barter/container/dialogue modes do
  not overlap or leave controls captured after the session ends.
- Multiplayer pause/menu screens do not pause server time or remote actors;
  communicate that state explicitly instead of relying on single-player pause
  assumptions.
- Add a bounded local dialogue watchdog that releases session/input state but
  does not rewrite quests or Papyrus state.

### 21. Harden NPC authority, movement and AI hosting

Status: planned improvement of existing client-hosted behavior.

- Stable server actor identity and canonical health/equipment/lifecycle state.
- Exactly one generation-bound AI host lease.
- Revoke old authority before host migration.
- Transfer a canonical snapshot before the new host simulates.
- Synchronize important outcomes and motion, not every internal AI decision.
- Recover appearance only from canonical equipment; never reset inventory.

### 22. Define combat, death, bleedout and revive state machines

Status: planned.

- `Alive -> Downed -> Reviving/Dead -> Respawned` with server revisions.
- Validate damage source, authority, timing and transition legality.
- Bound bleedout timers and prevent permanent knocked-down state.
- Define disconnect, LoadGame and host-migration behavior.
- Make death, revive, rewards and respawn idempotent.
- Keep full server-side Skyrim physics/hit simulation out of scope; validate
  outcomes and impossible input instead.

### 23. Implement followers and horses on shared ownership primitives

Status: concept only.

1. Personal follower with one owning player.
2. Generation-bound AI-host lease.
3. Canonical inventory/equipment ledger.
4. Owner-only commands and configurable behavior profiles.
5. Deterministic host failover and reconnect recovery.
6. Per-player and party-wide limits.
7. Separately gated party-owned quest followers.
8. Horse mount/dismount ownership using the same lease model.

### 24. Persist doors, locks, mechanisms, homes and selected world state

Status: planned.

- Stable object identity, canonical state and revision.
- Atomic open/close/lock/activate operations.
- Interaction leases for lockpicking and puzzles.
- Separate personal-home and party-home policies.
- Group storage and server reset epochs.
- Track only gameplay-significant world state, not every decorative object.

### 25. Scale quest support beyond the first P0 profiles

Status: planned.

- Extend analyzer templates and quest dependency graphs.
- Model branch exclusivity and adapter requirements.
- Add alias, scene and quest-object support only as separate proven capabilities.
- Require CI plus two-client evidence for every new profile family.
- Keep unknown or modified quests observation-only.

### 26. Synchronize time, calendar, weather and sleep policy

Status: planned improvement of existing STR synchronization.

- Server campaign clock with monotonic revisions and smooth client correction.
- Explicit leader/majority/unanimous sleep and wait policy.
- Server-selected weather transitions and stale-packet rejection.
- Permit local presentation differences indoors while preserving campaign time.

### 27. Define XP, rewards and progression policy

Status: planned.

- Configurable personal, shared, proximity or contribution-based XP.
- Idempotent reward operations.
- Explicit atomicity boundaries between quest completion, loot and XP.
- No reconnect/replay duplication.

### 28. Add player-facing UI and recovery guidance

Status: planned.

- Connection/reconnect/campaign and revision state.
- Interaction/follower ownership and host migration.
- Quest admission, quarantine and recovery reasons in plain language.
- Votes for sleep/time/shared decisions.
- Party roster, chat, nameplates and map/teammate indicators backed by stable
  player/session identity.
- Safe diagnostic-bundle export with advanced technical detail kept optional.

### 29. Harden server configuration and administration

Status: planned.

- Versioned configuration and migrations.
- Roles, permissions and audit trail.
- Follower, loot, respawn, XP and revive policies.
- Health/status reporting, backups and graceful shutdown.
- Reject incompatible server/client capabilities before joining a campaign.

### 30. Support game/mod versions through explicit capabilities

Status: planned continuous work.

1. Maintain a fully proven 1.6.1170 profile.
2. Add the exact current Steam runtime at release time through separate binary
   evidence and live validation.
3. Add future runtimes without speculative hooks.
4. Treat 1.5.97 as a legacy tier only when its complete test matrix is
   maintainable.
5. Bind SKSE, Address Library, bridge ABI, plugin mapping and scripts to the
   advertised capability set.

An update may disable an unsupported capability with an actionable message; it
must not silently call an old address.

### 31. Final security, performance and release validation

Status: planned recurring gate.

- Main-thread budgets and absence of repeated fingerprint scans.
- Bounded memory, queues, logs and reconnect storms.
- Lock ordering, sanitizer/static-analysis and MSVC/GCC/Clang portability.
- Malformed packet, unauthorized operation and replay-flood tests.
- Path/reparse confinement and crash-consistent migrations.
- Long multi-client sessions, mixed cells, host migration, save/load and
  upgrade/downgrade matrices.
- Release only the explicitly supported runtime/mod/profile matrix.
- Re-run the external upstream/issues/PR/add-on review and resolve every adopted
  candidate to an exact source SHA and local acceptance result.

## Immediate execution order

The next implementation action is Task 07, not Task 09 and not a broad NPC or
follower rewrite. The near-term order is:

1. Task 07 acceptance.
2. Task 08 acceptance, including Windows durability.
3. Narrow upstream runtime compatibility reconciliation.
4. Actor ownership epoch/stale-owner intake, replacing `ResetInventory` repair.
5. Observational diagnostics re-slice.
6. Tasks 09–15 in order.
7. Shared multiplayer foundation, first-party add-on API and gameplay phases
   16–31.
