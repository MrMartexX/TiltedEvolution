# Task 05 — Define reviewed compatibility admission and transition risk

Read `../MASTER-HANDOFF.md`. Start from the accepted integration HEAD after Task 04. Follow the master protocol. This task defines policy and validation; it does not populate the production profile list or enable mutation.

## Goal

Turn offline evidence into a strict, reviewable decision about which exact quest transitions may ever reach the runtime planner. Replace broad assumptions such as “stage-only” with explicit transition-specific preconditions and forbidden side effects.

## Required model

For each candidate transition record:

- exact quest/environment/runtime identity;
- source and target stage plus allowed current-state envelope;
- required prior stages/objectives and authoritative revision;
- expected quest running/stopped state;
- expected aliases, scenes, created references and player participation state;
- Papyrus fragments/scripts that may run;
- inventory/quest-object/world/alias mutations that make it forbidden;
- required readiness evidence and quiescence policy;
- expected postconditions and rollback/recovery class;
- reviewer/tool provenance and manifest version.

## Required work

1. Implement a pure deterministic validator/classifier for analyzer output. Unknown fields, missing evidence or conflicting overrides must be `Unsupported`/`NeedsReview`, never safe.
2. Separate structural eligibility, environment compatibility and authorization. Matching fingerprints alone do not authorize a mutation.
3. Reject backward stages, ambiguous graph edges, implicit multi-stage jumps, active scenes, player aliases, created references, inventory/quest-object changes, generic world changes and unbounded script effects unless a later explicit design supports them.
4. Define exact duplicate/replay behavior and transition identity. A newer authoritative revision retires older deferred candidates.
5. Sign or compile reviewed entries in a way runtime cannot confuse generated/unreviewed candidates with accepted profiles. Do not rely on editable local files as authority.
6. Ensure policy version changes invalidate incompatible cached decisions.

## Tests

Cover every rejection category; exact safe stage-only fixture; forward jump hiding intermediate fragment; same quest ID under changed plugin/script; load-order override; unknown alias/scene; player-specific and party-specific preconditions; stale revision; duplicate/replay; malformed/oversized manifest; mixed analyzer versions; hash collision defense/domain separation; deterministic ordering; exception/allocation failure with unchanged published registry.

Add property/fuzz tests for malformed candidate manifests if existing test infrastructure supports them.

## Done when

- Only an exact reviewed transition can become `Eligible`; eligibility still is not runtime mutation authority.
- Unknown/incomplete evidence is rejected with a stable reason.
- The validator is pure/testable and publication is atomic/fail closed.
- `BuildReviewedManifest()` may remain empty after this task.
- Canonical mutation remains disabled and all four CI jobs are FULL GREEN with exact counts.
