# Naked-NPC inventory reset duplication incident

Status: evidence snapshot; root cause proven, production fix not accepted

Evidence date: 2026-09-12 to 2026-09-13

Diagnostic code identity: local branch `diagnostics/interaction-save-state`,
head `e85a8f5b5f64ca9897783f464b542487f41f246b`.

This report preserves the observation separately from the future fix. The
diagnostic branch is not part of the accepted integration baseline.

## Reproduction

While connected to the local server, repeatedly steal an NPC's equipped
clothing. Immediately after the clothing is removed, new items can appear on
the NPC and be stolen again. Repeating the interaction produces an item-creation
loop rather than a finite transfer from one inventory.

## Correlated evidence

The captured client trace contained:

- nine automatic `ResetInventory` observations;
- two observations for Nazeem (`FormID 0x1A6A4`) during the relevant sequence;
- 881 inventory/equipment events without a usable actor/server identity;
- 1476 outbound inventory events;
- 215 outbound equipment events.

The associated server trace contained 534 `Entity is invalid: 45` reports with
InventoryService range-delivery failures. These identity failures require their
own root-cause slice; they do not by themselves prove the item creation.

## Proven root cause

`RunNakedNPCBugChecks()` periodically detects an actor without clothing and
calls `ResetInventory(false)`. Skyrim reconstructs the NPC's default inventory
or outfit while already stolen items remain with the player. The repair is not
an atomic transfer and has no conservation rule, so repeated stealing can
recreate the same source items indefinitely.

This explains the observed outfit/item duplication. It does not prove that
every dialogue, bleedout or NPC problem has the same cause.

## Violated invariant

Visual recovery must not create canonical items. For every successful transfer,
the total authoritative quantity across source, target and permitted world
locations must remain constant unless an explicit spawn/destruction operation
is committed by the server.

## Required narrow fix

1. Remove automatic broad `ResetInventory` from the naked-NPC repair path.
2. Re-equip only an item already present in the authoritative inventory/equipment
   snapshot.
3. When identity or canonical inventory is unavailable, leave the actor
   quarantined/visually unresolved and request resynchronization.
4. Do not invent a default outfit as fallback.
5. Add regression tests for repeated naked checks, duplicate equipment events,
   missing identity, reconnect and stale actor generation.
6. Validate item conservation in a repeated live pickpocket session.

## Later complete solution

Pickpocket, equipment and all inventory transfers should use one idempotent
server transaction with actor/player/item identities, expected revisions,
distance/line-of-sight checks and interaction ownership. That larger platform
does not justify leaving the proven creation loop enabled in the meantime.
