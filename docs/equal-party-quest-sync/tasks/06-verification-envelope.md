# Task 06 — Complete the runtime verification envelope

Read `../MASTER-HANDOFF.md`. Start from the accepted integration HEAD after Task 05. Follow the master protocol. Do not enable SetStage.

## Goal

Make the existing verification gate/monitor express and prove the complete precondition and postcondition envelope for one proposed quest transition. A successful native call alone must never equal a committed canonical result.

## Required envelope

Bind one immutable request to campaign, player/session, runtime generation, transaction, authoritative revision, operation ID, quest identity, compatibility profile/version and expected current/target state.

Preconditions must include:

- current process owner and guarded generation lease;
- active campaign/session/party/player ownership;
- non-retired transaction and exact authoritative revision;
- exact compatibility profile and cached environment evidence;
- fresh quest snapshot and safe transition edge;
- reference evidence for the same object/generation;
- supported Papyrus observer and bounded quiescence;
- valid durable checkpoint authorization when mutation is eventually attempted.

Postconditions must include the exact allowed stage/state, required objectives and absence of forbidden alias, scene, inventory, quest-object or world deltas. Define which observations are authoritative and how many stable samples are required.

## Required work

1. Audit `PartyQuestRuntimeVerificationGate`, monitor, authorization objects, apply request and mutation dispatch. Reuse them; do not add a parallel verifier.
2. Make validation plus dispatch/retirement one atomic/fenced decision. Close validate → generation/revision changes → execute TOCTOU.
3. Define bounded outcomes: Verified, Busy/Retryable, RejectedStale, Unsupported, Timeout, MutationFailed, PostconditionMismatch and ObserverUnavailable as existing APIs allow.
4. On any failure, do not publish success, advance replica state or retire recovery data incorrectly.
5. Prevent a precondition authorization token from being copied/reused for another request, generation, profile or target stage.
6. Keep diagnostics out of native failure paths and exceptions inside boundaries.

## Regression tests

Cover each identity mismatch; readiness true but authorization stale; generation change between validation and dispatch; revision N superseded by N+1; campaign/session/party change; observer unavailable; Papyrus never quiesces; pre-snapshot mismatch; forbidden side-effect surface; native false/exception; postcondition mismatch; duplicate completion; replayed authorization; checkpoint mismatch; cancellation/shutdown; failed validation leaves all state unchanged.

Add a state-machine test that enumerates every terminal outcome and proves exactly one of commit, deterministic recovery-required, or no-op/reject occurs—never two and never partial publication.

## Done when

- A verification authorization is single-operation, immutable and bound to every required identity.
- Validation cannot race lifecycle/revision invalidation before dispatch.
- Postconditions, not call return value, decide success.
- Failure leaves canonical/client state deterministic and mutation still disabled.
- All four CI jobs are FULL GREEN with exact Linux/Windows counts.
