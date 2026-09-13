# Server-authoritative multiplayer architecture

Status: normative target architecture

## Purpose

This document defines the ownership and safety model shared by equal-party
quest synchronization and later gameplay improvements. It does not claim that
every described subsystem is implemented. Implementation state belongs in
[CURRENT_STATUS.md](CURRENT_STATUS.md).

## Authority model

The server owns durable multiplayer truth. Party leadership is an
administrative role, not ownership of canonical state. Each player's local
Skyrim save is an independently evolving replica.

State is divided into three classes:

| Class | Examples | Model |
|---|---|---|
| Canonical and persistent | Quest progress, item ownership/counts, containers, NPC equipment, followers, important doors/locks | Server revisioned state and idempotent transactions |
| Transient synchronized simulation | Player/NPC movement, animation, combat target, short-lived interaction state | One generation-bound client lease; server relays and validates |
| Local presentation | Decorative physics, particles, sound, UI layout, non-authoritative cosmetics | Client-local unless explicitly promoted |

The project does not attempt to run a full deterministic Skyrim simulation on
the server. It synchronizes authoritative outcomes and grants narrow temporary
simulation leases where server-side simulation is impractical.

## Required identities

Every authoritative or deferred operation carries only the identities needed
for its domain, chosen from:

- stable campaign identity;
- server session and runtime generation;
- stable player profile and current network player identity;
- party identity when the operation is party-scoped;
- transaction and operation identity;
- expected world/domain revision;
- stable quest, actor, container, reference or item-stack identity;
- compatibility profile and environment fingerprint;
- attempt/capture nonce for asynchronous work.

Numeric FormID, a loaded pointer, a player ID or a quest stage alone is not a
stable authorization identity. Reuse after disconnect, LoadGame, campaign
switch or object reload must not pass validation by coincidence.

## Canonical transaction rule

Any action that can create, destroy, transfer or permanently change state is a
server transaction:

1. The client submits an intent with expected identities and revisions.
2. The server validates ownership, permissions and domain preconditions.
3. The server assigns or recognizes an idempotency key.
4. The canonical transition is committed atomically or rejected without a
   partial state change.
5. Duplicate/replayed requests return the prior result.
6. Clients apply the accepted result under a generation-bound local guard.
7. Verification or recovery retires the operation deterministically.

Unknown state is never converted into permission to continue.

## Runtime mutation trust chain

The quest path uses the strictest version of the common transaction rule:

```text
server canonical intent
  -> client canonical inbox
  -> runtime owner/session binding
  -> owner- and generation-bound deferred item
  -> readiness evidence
  -> consume-time authoritative revalidation
  -> compatibility admission
  -> Papyrus/quiescence observation
  -> durable PreRepair checkpoint
  -> narrow mutation adapter
  -> postcondition verification
  -> canonical acknowledgement or deterministic recovery
```

No earlier stage grants the authority of a later stage. In particular:

- admission is not mutation authority;
- a valid pointer is not lifecycle authority;
- `TESObjectLoadedEvent` is not authorization;
- returning from save-request admission is not save completion;
- file existence is not durable checkpoint publication;
- `SetStage` returning is not verified transaction completion.

## Lifecycle and leases

LoadGame, NewGame, Main Menu, disconnect, party leave, campaign switch,
shutdown and runtime generation transition revoke relevant leases before new
work may execute. Validation and side effect must share one lock/lease/fence
boundary; a check followed by an unguarded call is a TOCTOU defect.

Deferred work is immutable and owner-bound. Consumption revalidates campaign,
session, generation, transaction, revision, compatibility and readiness.
Duplicate readiness is idempotent. Stale work is retired without execution.

AI and interaction authority use short-lived leases. At most one client may
simulate or exclusively interact with a given authoritative actor/object in a
given lease generation. Failover revokes the old lease before publishing a new
one.

## Persistence model

Local `.ess` and `.skse` files remain per-player replicas. The server should not
store one opaque "true Skyrim save". The target campaign store is separated by
domain:

```text
campaign/
  manifest
  quests
  actors
  inventories
  containers
  world-state
  transactions
  snapshots
  checkpoints
```

Each domain has a schema version, monotonic revision, checksums and explicit
migration policy. An append-only operation journal plus compacted snapshots is
preferred to silent in-place mutation. Cross-domain operations either share a
defined transaction boundary or document that they are independent.

PreRepair and recovery files live only inside a confined campaign/player root.
Solo saves are never valid mutation targets. Strong mutation requires a
PowerLossDurable checkpoint and a deterministic restart recovery path on the
actual supported Windows filesystem.

## Compatibility and runtime support

Support is capability-based and tied to exact deployed identities:

- Skyrim executable identity, not only a numeric version;
- SKSE binary and bridge ABI;
- Address Library database identity;
- loaded plugin mapping and winning override fingerprints;
- relevant script/native-adapter fingerprints;
- reviewed compatibility-profile version.

An unknown game update disables unsupported hooks and mutation paths with an
explicit reason. It must not try a nearby address or ABI. A supported runtime
profile is promoted only after both automated and live evidence.

## Inventory and object conservation

The server records item/stack identity, location, quantity and revision.
Transfers are atomic source-to-target transactions. Visual repair may re-equip
an item already present in canonical inventory, but it may not create a default
outfit, call broad `ResetInventory`, or infer missing inventory from appearance.

For leveled loot, one accepted observation publishes the generated result. The
project synchronizes that result rather than attempting to synchronize random
number generators across saves.

## NPC, follower and interaction authority

Important NPCs have stable server identities, canonical health/equipment state
and one current AI-host lease. Movement and animation are transient; inventory,
equipment, alive/downed state and follower ownership are canonical.

Dialogue, barter, transfer and pickpocket use explicit interaction sessions.
Distance, line of sight, actor state, ownership and expected revision are
revalidated at commit. Conflicting interaction, combat, damage, disconnect or
lease expiry cancels the operation.

Personal followers belong to one player. Quest followers belong to the
campaign/party under a separate policy. Both use the same lease and inventory
transaction foundations; neither is implemented through client-local ownership
guessing.

## Error and exception boundary

- Validation, observer, allocation, persistence and lookup failures leave the
  protected domain closed.
- C++ exceptions never cross native Skyrim, SKSE or Papyrus ABI boundaries.
- Diagnostics record the reason but do not turn failure into success.
- Recovery cannot guess a checkpoint or silently adopt ambiguous evidence.
- Tests may not bypass production gates or enable broader authority.

## Explicit non-goals until separately proven

- full server simulation of Skyrim physics and AI;
- synchronization of every decorative object's exact transform;
- automatic repair of unknown quest graphs or incompatible saves;
- generic alias, quest-object, inventory or world mutation through the quest
  adapter;
- broad support for a game/SKSE version based only on its version string;
- enabling functionality because one live session happened not to crash.
