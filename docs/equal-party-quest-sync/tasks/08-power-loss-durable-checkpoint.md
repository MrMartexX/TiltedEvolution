# Task 08 — Publish a PowerLossDurable PreRepair checkpoint

Read `../MASTER-HANDOFF.md`. Start only if Task 07 produced and integrated a proven save contract. Follow the master protocol. Do not enable SetStage in this task.

## Goal

Connect the existing checkpoint/workspace/sidecar primitives to the proven engine-save contract and establish a checkpoint that survives process crash and realistic power interruption before any future canonical mutation is authorized.

## Required transaction

1. Acquire exact owner/generation/campaign/profile/transaction authorization.
2. Create an isolated unique attempt directory and engine save name.
3. Capture matching `.ess` and `.skse` through the proven contract.
4. Validate file identity, non-zero/bounded size and expected pair provenance.
5. Persist metadata containing schema, campaign/profile, generation, transaction/revision, quest/transition/profile fingerprints and artifact hashes.
6. Flush file contents, metadata and required directory entries using platform-correct durability primitives.
7. Atomically publish one committed checkpoint marker only after every artifact is durable.
8. Clean or quarantine incomplete attempts without replacing the last known-good checkpoint.

## Safety requirements

- Use existing `PartyQuestRuntimePreRepairCheckpoint`, durable workspace/snapshot/sidecar and lease primitives.
- One writer per campaign/profile/transaction namespace; prove cross-thread and cross-process exclusion where required.
- Path construction must resist traversal, symlink/reparse-point escape, case/Unicode aliasing and namespace collision.
- Atomic rename alone is not a power-loss guarantee. Document Windows and Linux file/directory flush semantics and what the policy can honestly claim.
- `AllowsNativeRuntimeMutation()` remains false until this task's durable guarantee and later recovery/live gates are accepted.

## Fault-injection tests

Inject failure after each write/flush/rename/publication step; disk full; access denied; corrupt/truncated `.ess`, `.skse`, metadata or marker; missing co-save; stale generation; concurrent attempt; lock-owner death; pre-existing conflict; cleanup failure; exception/allocation failure. After restart, recovery must select only a fully committed, hash-valid exact-identity checkpoint.

Run Linux and Windows filesystem tests on real filesystems, not only in-memory mocks. Record exact guarantee differences.

## Done when

- A successful result means both save artifacts and metadata are durably published as one exact checkpoint.
- Every interrupted prefix is rejected or recoverable without losing the prior good checkpoint.
- Ordinary Skyrim saves are untouched.
- Mutation remains disabled; all four CI jobs are FULL GREEN with exact counts and filesystem evidence is reported.
