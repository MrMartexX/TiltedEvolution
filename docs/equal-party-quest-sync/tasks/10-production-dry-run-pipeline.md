# Task 10 — Connect the production pipeline in strict dry-run mode

Read `../MASTER-HANDOFF.md`. Start from the accepted integration HEAD after Task 09. Follow the master protocol. This task connects components but must keep native canonical mutation physically disabled.

## Goal

Connect the actual client path end to end:

server canonical update/repair → admitted runtime candidate → process owner → deferred queue → readiness evidence → consume-time revalidation → compatibility → quiescence → checkpoint planning → verification decision → deterministic dry-run result/retirement.

## Required work

1. Trace `QuestService` protocol handlers and `PartyQuestRuntimeCanonicalInbox`. Choose one owner for each pending request; no duplicate shadow queue.
2. Convert only fully admitted server state into an immutable runtime request carrying campaign, session/player/party, generation, transaction, revision, operation and quest/profile identities.
3. Enqueue through existing owner-bound deferred-world APIs. Missing reference may defer; missing authority may not.
4. On readiness/update consumption, repeat all authoritative checks under the existing guarded/fenced boundary. Close validation→dispatch TOCTOU.
5. Invoke compatibility cache, runtime safety planner, Papyrus/quiescence observer and verification gate in deterministic order.
6. Exercise checkpoint eligibility and recovery decision without performing engine save or mutation unless prior accepted tasks explicitly made a safe non-mutating capture test path available.
7. Produce one explicit dry-run terminal result: would-apply, stale, unsupported, retryable-busy, timeout, recovery-blocked or invalid. Do not ACK canonical mutation success.
8. Invalidate/reject pending work on disconnect, party leave, campaign switch, LoadGame, NewGame, MainMenu and shutdown.
9. Keep expensive compatibility work off the Skyrim update thread. Consumption must have a measured bounded budget.

## Regression tests

End-to-end tests must cover success-shaped dry run; missing mapping/reference; readiness duplicate; duplicate server operation; stale generation/campaign/session/party/revision; out-of-order N/N+1; observer unavailable/busy; compatibility mismatch; checkpoint unavailable; invalidation concurrent with consume; shutdown pending work; exception at each boundary; no partial replica/transaction publication.

Add assertions that `PartyQuestSkyrimStageMutationExecutor` is not invoked for dry-run, `TESQuest::SetStage` is absent, and no aliases/inventory/world/save files are changed.

## Live evidence

On Skyrim 1.6.1170, connect to the server and drive real quest updates through the pipeline. Logs must show exact request identities and dry-run outcomes while gameplay remains responsive. Specifically retest the Dragonsreach scenario that previously caused 10–50 second hangs.

## Done when

- Real production messages reach the existing safety pipeline and terminate deterministically without mutation.
- All lifecycle, duplicate and stale paths are proven fail closed.
- The former fingerprint freeze is absent in live play with timing evidence.
- All four CI jobs are FULL GREEN with exact counts; mutation remains disabled.
