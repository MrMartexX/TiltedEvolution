# Equal-party quest synchronization PoC

## Goal

Extend Skyrim Together Reborn so a party has one canonical quest journal while any member may initiate the next valid quest action. The party leader remains an administrator, not the exclusive quest authority.

This proof of concept does not change live quest behavior yet. Its first milestone defines and validates a deterministic `QuestSnapshot` representation.

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

1. Collect stage, status, objectives, reference aliases, and location aliases from `TESQuest`.
2. Log snapshots from two clients.
3. Validate safe restoration of alias values.
4. Define a compatibility fallback if direct alias restoration is unsafe.

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

## Current commit scope

The initial commit adds only:

- the `QuestSnapshot` data model;
- deterministic canonicalization;
- a stable FNV-1a digest;
- unit tests for order independence, mismatch detection, and duplicate removal;
- reference JSON schemas.

No runtime hooks, messages, save changes, or quest mutations are introduced.
