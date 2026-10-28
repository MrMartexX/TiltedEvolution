# Task 15 — Final adversarial audit and P0 acceptance decision

Read `../MASTER-HANDOFF.md`. Start only after Tasks 01–14 are integrated. Prefer an engineer/agent that did not author the last implementation slices. Begin read-only; do not assume earlier reports are correct.

## Goal

Independently reconstruct the complete trust chain, reproduce critical tests/live evidence and decide honestly whether P0 can close. Search for forgotten paths and counterexamples, not merely checklist presence.

## Audit procedure

1. Live-verify PR, integration HEAD, branch ancestry, changed range, clean checkout, all workflow runs and exact TPTests counts. Inspect logs to ensure build/test steps were not skipped.
2. Trace real production code from server canonical transaction through client protocol, owner/bootstrap, queue, consume-time validation, compatibility, observer/quiescence, durable checkpoint, narrow mutation, postconditions, ACK/commit and recovery/retirement.
3. Prove every authorization identity and source: campaign, server session/player/party, runtime generation, profile lineage, transaction, revision, operation, quest, compatibility profile and readiness object.
4. Attempt TOCTOU/ABA/reentrancy counterexamples at each handoff. Inspect lock order, lease lifetime, atomics/memory order, pointer/handle lifetime, callbacks, shutdown and exception boundaries on MSVC and GCC/Clang.
5. Confirm no alternate path calls `TESQuest::SetStage` or other forbidden mutation without the same envelope. Search for aliases, inventory, quest-object and generic world mutation.
6. Re-audit protocol ordering, duplicate/replay, missing-mod partial mapping, campaign switch, disconnect, party leave, LoadGame/NewGame/MainMenu, shutdown and recovery.
7. Reproduce analyzer/profile fingerprints and prove the shipping artifact matches the reviewed environment and source SHA.
8. Verify checkpoint/recovery durability claims against actual Windows filesystem behavior and Task 14 evidence.
9. Re-run the two-client critical path and a smaller failure/recovery smoke test on the exact proposed final artifact.

## Corrective work rule

If any defect is found, declare P0 NOT CLOSED immediately for that HEAD. Fix one root cause in a separate narrow branch with regression test and full CI, integrate it, then restart the affected audit portions. Do not stack unrelated fixes or waive a gate.

## Production activation decision

Shipping/default narrow mutation may be enabled only after the read-only audit finds every master gate satisfied and the controlled Task 13–14 evidence applies to the exact candidate code. Activation itself is a separate minimal commit, followed by all four CI jobs, exact counts, artifact hash and final live smoke/recovery verification.

If safety currently depends on a test-only flag, local patch, uncommitted config or different SHA, P0 is not closed.

## Required final report

- `P0 CLOSED` or `P0 NOT CLOSED` as the first line.
- PR state; integration/final/last-fully-green SHA; artifact hash; full audited diff range.
- Linux/Windows Build and diagnostics results with exact assertion/test-case counts.
- Architecture/trust-chain proof and race/lifetime analysis.
- Reviewed quests/transitions and precise unsupported surface.
- Two-client/lifecycle/crash/power-loss evidence with artifact/log locations.
- Confirmation of ordinary-save isolation and absence of forbidden authority expansion.
- Every remaining uncertainty/blocker and the next exact action.

## P0 CLOSED criteria

All master gates must hold on one exact production candidate SHA and artifact, with shipping/default narrow functionality enabled only for reviewed profiles. “All unit tests pass”, dry-run-only behavior, controlled-only activation, missing live power-loss evidence when PowerLossDurable is required, or any unsupported authoritative observer is insufficient.
