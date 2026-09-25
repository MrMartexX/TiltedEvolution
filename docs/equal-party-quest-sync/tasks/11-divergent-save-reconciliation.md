# Task 11 — Plan safe reconciliation of divergent local saves

Read `../MASTER-HANDOFF.md`. Start from the accepted integration HEAD after Task 10. Follow the master protocol. This task remains non-mutating.

## Goal

Given server canonical quest state and a client's independent local save, deterministically classify the relationship and produce either an exact admissible repair candidate or a fail-closed reason. Different stage numbers are not enough to infer a safe path.

## Required classifications

- Exact/equivalent: required canonical facts already match.
- Locally behind on one reviewed transition edge.
- Locally ahead but compatible with canonical intent.
- Divergent branch/objectives/aliases/scenes/scripts.
- Missing/not-started/stopped/failed quest.
- Different mod/runtime/profile environment.
- Insufficient or unstable observation.
- Recovery-required because a prior transaction is unfinished.

## Required work

1. Define a pure planner input containing authoritative campaign/transaction/revision, exact reviewed profile, local snapshot, prior committed/retired operations and runtime generation.
2. Compare the full allowed compatibility/verification envelope—not only `CurrentStage`.
3. Permit only explicit reviewed graph edges. Never synthesize intermediate stages, reverse a quest, skip fragments or replay side effects.
4. If local state is safely ahead, define whether no-op confirmation is valid without overwriting server authority; do not make the local save canonical.
5. A server repair plan and ordinary canonical update must share one transaction/revision ordering rule. N+1 retires deferred N.
6. Distinguish RetryAfterFreshSnapshot from Unsupported and RecoveryRequired. Bound retries and prevent request loops.
7. Preserve missing-mod partial mapping semantics: missing local quest/mod is a clear unavailable result, not a guessed FormID.
8. Output an immutable candidate for the existing dry-run pipeline. No direct engine pointers and no mutation.

## Tests

Cover exact match; one-edge behind; multiple-stage gap; ahead compatible/incompatible; alternate branch with same stage; stopped/failed/not-started; objective mismatch; active scene/player alias; created reference; different plugin/script fingerprint; missing mod; stale revision; equal-current-stage duplicate; two different target stages delivered in both orders; legitimate repeatable edge; N then N+1; replay after reconnect; late Papyrus echo; recovery pending; observer unavailable; snapshot changes during plan; campaign/profile/generation ABA; deterministic repeated planning.

Include table-driven cases where identical quest/Form IDs in two campaigns must not compare as the same operation.

## Done when

- Every pair of server/local states has an explicit deterministic classification.
- Only an exact reviewed transition can produce a repair candidate.
- No local-save state becomes server authority and no unsafe “just set the number” fallback exists.
- Mutation remains disabled and all four CI jobs are FULL GREEN with exact counts.
