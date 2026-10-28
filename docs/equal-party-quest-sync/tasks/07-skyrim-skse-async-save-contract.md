# Task 07 — Prove the Skyrim/SKSE async save contract

Read `../MASTER-HANDOFF.md`. Start from the accepted integration HEAD after Task 06. Follow the master protocol. This is a proof/instrumentation task; do not re-enable PreRepair capture until the contract is demonstrated.

## Goal

Determine exactly when Skyrim and SKSE capture save paths and when both `.ess` and matching `.skse` files are complete, so an isolated PreRepair checkpoint cannot escape into normal saves or be published while still being written.

## Required research

Trace the exact 1.6.1170 code path:

`Save_Impl` request → save queue/request object → worker execution → main save naming/path resolution → file creation/replace → SKSE serialization callback/co-save path resolution → completion/failure notification.

Use matching Bethesda-observable binary evidence, CommonLibSSE-NG and SKSE 2.2.6 source. Record exact runtime/binary identity and all Address Library IDs/signatures used. Do not infer completion from `Save_Impl` returning.

## Questions that must be answered

1. Is `sLocalSavePath` copied into an owned request before `Save_Impl` returns, or read later by a worker/SKSE hook?
2. Is the save name/path immutable for both `.ess` and `.skse`?
3. What authoritative event proves the main file is closed and fully written?
4. What proves SKSE serialization completed successfully for the same save?
5. Can another manual/auto save overlap? What lock/queue ownership serializes requests?
6. What happens on cancellation, failed co-save, disk full, exception, LoadGame, MainMenu or shutdown?

## Implementation constraints

- Prefer an owned per-request path/name carried through the real pipeline over temporary mutation of a process-global setting.
- If no safe per-request injection seam exists, keep engine checkpoint disabled and report the exact blocker.
- Completion must identify the exact request, generation, campaign/profile and both file artifacts; polling mere file existence is insufficient.
- Any instrumentation/hook must be read-only until signatures/lifetimes are proven and must fail closed on unsupported binaries.
- Never overwrite or redirect the user's ordinary saves. Use a controlled isolated test root and disposable test save.

## Tests and live proof

Unit/integration tests: request identity; delayed path lookup; overlapping saves; out-of-order completion; `.ess` success/`.skse` failure and inverse; stale completion; timeout; disk-full/write failure; generation transition; cancellation/shutdown; pre-existing filename; cleanup after partial attempt.

Live test on the isolated 1.6.1170/MO2 environment must record timestamps/IDs showing when path capture and both completions occur. Verify ordinary manual/autosaves stay in their original location and no temporary global path remains changed.

## Done when

- There is a source- and live-evidence-backed completion contract for both files, or a precise blocker proving no safe seam exists.
- No engine save side effect is enabled on assumption alone.
- Canonical mutation remains disabled and required CI is FULL GREEN with exact counts.
