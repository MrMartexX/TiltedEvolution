# Equal-party quest synchronization documentation

Status: canonical subsystem index

## Read in this order

1. [Current project status](../project/CURRENT_STATUS.md)
2. [Project architecture](../project/ARCHITECTURE.md)
3. [P0 master handoff](MASTER-HANDOFF.md)
4. [Ordered task package](tasks/README.md)
5. The technical document relevant to the current task

## Normative project documents

- [Implementation roadmap](../project/IMPLEMENTATION_ROADMAP.md)
- [Documentation policy](../project/DOCUMENTATION_POLICY.md)
- [PoC scope](POC_SPEC.md) — original scope and gameplay model; embedded
  milestone statuses are historical.

## Technical design and evidence map

| Document | Role |
|---|---|
| [CI validation](CI_VALIDATION.md) | Accumulated automated-validation design and historical evidence |
| [Player replica layout](PLAYER_REPLICA_LAYOUT.md) | Campaign/player filesystem identity and confinement model |
| [Persistence durability](PERSISTENCE_DURABILITY.md) | Durability guarantees, platform boundary and closure checklist |
| [Restore journal](RESTORE_JOURNAL.md) | Restore transaction/journal domains |
| [Runtime apply guardrails](RUNTIME_APPLY_GUARDRAILS.md) | Native-apply protection boundary |
| [Runtime crash recovery](RUNTIME_CRASH_RECOVERY.md) | Recovery coordinator and remaining strong-routing boundary |
| [Campaign-state schema](party_quest_state.schema.json) | Persisted campaign state schema |
| [Quest snapshot schema](quest_snapshot.schema.json) | Diagnostic/canonical quest snapshot schema |

Task 07 implementation documentation is intentionally merged with its code,
not copied into the accepted baseline early. Its accepted status is tracked in
[CURRENT_STATUS.md](../project/CURRENT_STATUS.md).

## Safety boundary

- Canonical `TESQuest::SetStage` remains disabled until the ordered P0 gates are
  accepted on one exact production candidate.
- Readiness is evidence, never authority.
- Local saves are independent player replicas.
- Unknown ownership, compatibility, observer or recovery state fails closed.
- Quest aliases, quest objects, inventories and generic world state are not
  part of the initial narrow mutation adapter.

No individual technical document can override these boundaries or declare P0
closed.
