# Diagnostics documentation

Status: planned and evidence index

Diagnostics are observational support. They may explain or correlate behavior,
but they never grant gameplay authority, repair a save or turn an unknown state
into a successful operation.

## Current material

- [Naked-NPC inventory reset incident](NPC_RESET_INVENTORY_INCIDENT.md) — live
  evidence for the repeated pickpocket/outfit-duplication defect.
- Interaction/save-state recorder implementation exists only on local branch
  `diagnostics/interaction-save-state` at the status snapshot. It is not part of
  the accepted integration baseline.

## Planned accepted diagnostic surface

- Disabled by default.
- Bounded JSONL records with schema version and exact build identity.
- Campaign/session/runtime-generation/load-epoch correlation.
- Dialogue, container, barter, inventory, equipment, death/bleedout and
  recovery observations.
- Process-local event sequence plus real server operation identity when one is
  available; the two must not be confused.
- Buffered writes and rotation so logging does not stall the game thread.
- Offline analysis that separates observations, correlations and proven causes.
- A four-run save comparison: suspect offline, suspect connected, fresh offline
  and fresh connected under the same executable/mod/test steps.

Absence of a symptom in one run is not proof that a save is healthy.
