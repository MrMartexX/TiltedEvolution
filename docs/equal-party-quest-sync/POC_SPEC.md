# Equal-party quest synchronization PoC

## Goal

Extend Skyrim Together Reborn so a party has one canonical quest journal while any member may initiate the next valid quest action. The party leader remains an administrator, not the exclusive quest authority.

The proof of concept is intentionally incremental. It first validates deterministic snapshots, then reads live Skyrim quest state without changing saves or network behavior.

## Agreed gameplay model

- One canonical quest state per party.
- Any party member may start, advance, fail, stop, or complete a shared quest.
- Accepted quest changes are propagated to every party member immediately.
- A reconnecting client is repaired from the server's canonical state.
- Reference aliases, location aliases, objectives, completed stages, and created references are compared to detect hidden-state divergence.
- Radiant quest results are synchronized from the initiating client instead of being independently regenerated.
- Quest items are intended to be mirrored across party inventories in a later milestone.
- Character-specific states such as werewolf or vampire transformation remain personal.
- Only one player may own the active dialogue UI for a given NPC at a time; different NPCs may be used concurrently.
- Vanilla audio and subtitles remain unchanged.
- AI and Mantella integration are outside this project phase.

## Milestones

### PoC A: deterministic snapshot and digest

Status: complete and validated by Windows, Linux, and dedicated test CI.

1. Represent fixed quest state independently from runtime pointers.
2. Canonicalize unordered collections.
3. Compute a stable digest that does not depend on collection insertion order.
4. Detect a deliberate alias or objective mismatch in unit tests.

### PoC B: server-owned state and repair

1. Store the latest accepted snapshot per party and quest.
2. Add a monotonically increasing revision.
3. Repair a client that joins with an older quest state.
4. Suppress network echo while applying a remote repair.

### PoC C: runtime collector and aliases

Status: runtime collection is in progress.

Implemented read-only collection:

1. Quest status, including failure.
2. Current stage and completed stages.
3. Objective indices and runtime states.
4. Reference alias IDs and resolved references when they map to a stable `GameId`.
5. The alias `Quest Object` flag.
6. Created references when they already have a stable mapped ID.
7. Location alias identities.
8. Diagnostic snapshots after local and replicated quest events.

Still required:

1. Resolve the selected `BGSLocation` behind a location alias.
2. Assign party-owned IDs to dynamic references that cannot use a normal plugin `GameId`.
3. Compare logs from two live clients.
4. Validate safe restoration of alias values.
5. Define a compatibility fallback if direct alias restoration is unsafe.

### PoC D: mirrored quest items

1. Detect active aliases marked as quest objects.
2. Mirror add, remove, and replacement operations to party members.
3. Preserve supported instance data.
4. Suppress replicated inventory events from being re-emitted.

### PoC E: NPC interaction lease

1. Grant one short-lived dialogue lease per NPC.
2. Release it when dialogue closes, the owner disconnects, leaves the cell, or times out.
3. Do not block dialogue with unrelated NPCs.

## Canonical-state rule

- A new valid local game event is authoritative for that transaction.
- A replicated event is never treated as a new source event.
- An unexplained mismatch is repaired from server state.
- A reconnecting stale client is repaired from server state.
- Manual administrator selection is reserved for failed automatic repair.

## Current implementation safety boundary

The runtime collector is observational only. It does not:

- send snapshots over the network;
- change quest stages or objectives;
- fill, clear, or replace aliases;
- add or remove inventory items;
- modify saves;
- change dialogue ownership.

Location aliases are listed by alias ID but their selected location remains unresolved until a dedicated accessor is validated. Dynamic references that cannot be converted to a stable plugin `GameId` remain explicitly unresolved rather than being compared using client-local form IDs.
