# Task 09 — Complete deterministic crash and restart recovery

Read `../MASTER-HANDOFF.md`. Start from the accepted integration HEAD after Task 08. Follow the master protocol. Do not enable SetStage.

## Goal

Wire the existing recovery/restore primitives into a deterministic protocol that can distinguish no mutation, committed mutation and uncertain mutation, and can restore the exact protected co-op replica without touching unrelated saves.

## Required work

1. Define durable transaction phases and legal restart transitions: Prepared, CheckpointCommitted, MutationAttempted, VerifiedCommitted, RecoveryRequired, Restored/Retired.
2. Persist phase changes with the same exact identity envelope and monotonic ordering. Never infer success solely from an absent process or a native return value.
3. On startup/rebind, inspect and authenticate only the exact campaign/profile namespace under a held workspace lease.
4. Reconcile durable metadata, `.ess`, `.skse`, journal/sidecar and authoritative server revision. Unknown/mixed evidence must fail closed and request recovery/manual intervention.
5. Restore only through a proven engine LoadGame contract and fresh lifecycle generation. A restored save must not reuse the pre-crash authorization token.
6. After restore, resnapshot and compare required postconditions before rejoining mutation flow. Notify server using existing protocol semantics; do not invent silent local success.
7. Preserve last-known-good checkpoint until verified retirement is itself durable.
8. Bound retries; avoid boot loops and repeated destructive restore attempts.

## Regression/fault tests

Crash at every phase boundary; torn/corrupt phase record; checkpoint from another campaign/profile/runtime; server revision advanced; duplicate restart; restore succeeds but co-save fails; load failure; resnapshot mismatch; stale generation; concurrent process/lease conflict; cleanup interruption; already-committed replay; unknown outcome; manual ordinary save present nearby.

Prove idempotence across repeated recovery startup and that an old checkpoint cannot roll back a newer authoritative revision.

## Live validation

Using only disposable isolated co-op saves, terminate the client at controlled phase boundaries and verify restart behavior/logged decision. Do not simulate power-loss claims solely by ordinary process exit; reserve actual power-interruption evidence for Task 14.

## Done when

- Every durable evidence combination has one deterministic outcome.
- Recovery never guesses, crosses campaign/profile boundaries or mutates ordinary saves.
- The client cannot publish commit until post-restore/post-mutation verification succeeds.
- Mutation remains disabled; all required CI is FULL GREEN with exact counts and controlled crash evidence is reported.
