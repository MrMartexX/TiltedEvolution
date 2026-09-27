# Task 13 — Implement narrow SetStage dispatch and validate two clients

Read `../MASTER-HANDOFF.md`. Start only after Tasks 01–12 are accepted and their blockers are zero. Follow the master protocol. This is the first task allowed to add the canonical `TESQuest::SetStage` call, and only behind the complete envelope below.

## Goal

Activate one reviewed stage-only transition in a controlled validation artifact, then prove server-authoritative behavior with two independent client saves. Do not make broad/global quest mutation available.

## Mandatory preflight gate

Before writing the call, re-audit and record that all are true on the current HEAD:

- production owner/bootstrap and lifecycle coverage are active;
- exact 1.6.1170 Papyrus observer is supported;
- reviewed profile and environment cache match;
- durable PreRepair checkpoint and deterministic recovery are operational;
- production dry-run pipeline passes real server messages;
- verification envelope closes generation/revision TOCTOU;
- `AllowsNativeRuntimeMutation()` truthfully represents PowerLossDurable support.

If any item is false, do not add/enable SetStage; return the exact blocker.

## Minimal implementation

1. Add the native call only inside `PartyQuestSkyrimStageMutationExecutor` after its existing main-thread, narrow-plan, FormID round-trip, snapshot, stage-existence, monotonic-stage and durability checks.
2. Require the exact reviewed quest/edge authorization, current process-owner guard, generation lease, transaction/revision token, fresh compatibility facts, quiescence and committed checkpoint authorization.
3. Hold the atomic/fenced execution boundary so lifecycle/revision invalidation cannot occur between final validation and the call.
4. Treat the native return as attempt evidence only. Resnapshot and verify all postconditions; only then publish client commit/ACK.
5. On false, exception, timeout or mismatch, enter deterministic recovery; never retry SetStage blindly.
6. Equal target stage is idempotent no-op. Duplicate/replayed transaction cannot call the engine twice.
7. Keep aliases, inventory, quest objects and generic world mutation structurally unreachable.
8. Use an explicit controlled-activation capability for the live artifact. Shipping/default activation remains off until Task 15 accepts all evidence.

## Tests

Mock/adapter tests: exact success; equal-stage no-op; competing N/N+1 targets delivered in both orders; legitimate repeatable profile edge; every missing authorization; wrong quest/edge/profile; stale generation/campaign/session/party/revision; duplicate/replay after reconnect; late local Papyrus echo after the adapter returns; invalid FormID round trip; nonexistent/backward target; changed pre-snapshot; lifecycle invalidation race; SetStage false/exception; postcondition mismatch; recovery transition; no double call; no forbidden mutation API reachable.

## Two-client live matrix

Use two separate MO2 profiles/save roots/player lineages against the same server campaign:

1. Client A at reviewed source state; Client B already equivalent.
2. Client A behind; Client B at target.
3. Both receive the same canonical operation concurrently.
4. Duplicate/replayed update.
5. One client temporarily lacks readiness, then becomes ready.
6. Different unrelated quest progress on each save remains untouched.

Record server revision/transaction, each client generation/profile, checkpoint IDs, one native attempt at most, pre/post snapshots, ACK/recovery result and logs. Keep disposable backups and never use ordinary user saves.

## Done when

- The controlled artifact performs exactly one authorized transition and both clients converge on verified canonical facts.
- No unrelated quest/save/world state changes and no duplicate side effect occurs.
- Any failure recovers or remains safely blocked.
- Shipping/default activation is still off pending final audit.
- All four CI jobs are FULL GREEN with exact counts and complete two-client live evidence is attached.
