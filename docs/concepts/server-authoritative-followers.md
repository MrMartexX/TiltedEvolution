# Server-authoritative followers concept

Status: planned concept only. This document does not authorize production
mutation, alter the wire protocol, or change follower behavior.

## Goal

Support personal followers for individual players and temporary shared quest
followers without allowing two clients to control the same actor, duplicate
inventory, or silently damage local quest/save state.

## Proposed model

### Personal followers

Each personal follower has a stable identity and exactly one owning player.
Only that player may issue commands, open the managed inventory, change
equipment, or dismiss the follower. Other party members observe and interact
only through explicitly permitted group actions.

### Shared quest followers

Quest-bound temporary followers belong to the party/campaign rather than an
individual player. Their authority is assigned by an explicit policy (normally
party leader or server orchestration) and is retired when the quest relationship
ends. A local quest state conflict blocks activation instead of being repaired
speculatively.

### Server authority

The server is the canonical source for:

- follower identity and kind (personal or quest-bound);
- owning player and party/campaign identity;
- recruitment state and current command;
- current AI-host lease;
- health/knockdown state relevant to replication;
- inventory and equipment ledger;
- last committed operation/revision.

Clients may simulate the actor only while holding a valid generation-bound
lease. Pointer or reference readiness is evidence, never authority.

## AI hosting and failover

Normally the owner's client hosts follower AI. A lease binds that authority to
the session, campaign, runtime generation, actor identity, and revision. On
disconnect, load, party leave, campaign change, or lease expiry, the old lease
is revoked before another client can acquire it. The replacement host receives
a canonical snapshot and must acknowledge it before simulation resumes.

No two clients may hold an execution lease for one follower at the same time.
Unknown ownership or unavailable authoritative state fails closed.

## Inventory and anti-duplication

Every transfer is one idempotent server transaction rather than independent
local add/remove actions. It contains an operation identity, source, target,
item/stack identity, amount, expected revisions, and owner authorization. The
server validates and commits the transfer atomically; replays return the prior
result without applying a second side effect.

Automatic `ResetInventory` is not permitted. Visual recovery may only re-equip
an item already present in the canonical ledger. It must never create default
items, erase player-provided equipment, or run for an unidentified actor.

## Save and quest safety

Connection/recruitment begins with a read-only comparison of local and server
state. Actor identity, recruitment state, quest relationship, party/campaign,
and runtime generation must agree. A conflict quarantines the follower and
reports the reason; it does not rewrite quests, factions, aliases, or the save.

Local saves remain replicas. Any future repair requires a separately proven
checkpoint, verification, and recovery contract.

## Player-facing flexibility

Servers may configure one follower per player and a separate party-wide cap.
Once the authority model is proven, personal behavior profiles may include
combat range, aggression, formation distance, protection target, support role,
wait/follow, and move-to commands.

## Suggested implementation order

1. Remove unsafe inventory reconstruction and reject stale actor identities.
2. Introduce stable follower ownership and generation-bound AI-host leases.
3. Add the authoritative, revisioned inventory/equipment ledger.
4. Add deterministic lease transfer and reconnect recovery.
5. Add owner-only commands and configurable behavior profiles.
6. Add separately gated party-owned quest followers.
7. Validate disconnect, reconnect, load, party change, duplicate/replay, host
   migration, save conflicts, and power interruption with automated and live
   tests.

## Non-goals for the initial slice

- enabling canonical quest mutation;
- automatically repairing incompatible saves;
- synchronizing every world item as part of follower ownership;
- changing follower limits before the ownership and inventory model is proven.
