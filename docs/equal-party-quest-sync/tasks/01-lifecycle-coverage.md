# Task 01 — Prove production lifecycle coverage

Read `../MASTER-HANDOFF.md` first and follow its branch/CI/reporting protocol. Work only on this task.

## Goal

Make the existing runtime owner/generation fence provably cover every Skyrim character-identity transition before state changes: LoadGame, NewGame and return to Main Menu. This task establishes the prerequisite for production session bootstrap; it does not bind a session and does not enable mutation.

## Starting points to verify

- `Code/client/Games/Skyrim/SaveLoad.cpp`: `Load_Impl`, `StartNewGame`, `TESLoadGameEvent` completion.
- `Code/client/SkyrimVM64.cpp`: main-loop reset/Main Menu boundary.
- `PartyQuestRuntimeLifecycleIntegrationPolicy`, `PartyQuestRuntimeLifecycleFence`, `PartyQuestRuntimeGenerationFence`.
- Post-MinHook commit validation in `PartyQuestSkyrimNativeHookValidation`.

Do not trust names, comments or Address Library numbers as proof. Trace installer → target resolution → queued hook → actual commit validation → pre-transition fence → original function → completion/invalidation.

## Required work

1. Prove the exact supported runtime identities and hook targets; validate executable section, expected entry/prologue evidence and ABI. Numeric Skyrim version alone is insufficient when multiple binaries share it.
2. Ensure `HasCompleteCharacterIdentityCoverage()` can become true only after all three hooks were actually created/enabled and validated. Resolution or queueing alone must remain insufficient.
3. For each transition, acquire the exclusive lifecycle/generation boundary before engine identity can change. If owner recovery, lock acquisition, hook validation or original target is unavailable, fail closed.
4. Define completion semantics:
   - LoadGame: admitted request paired with authoritative completion; unpaired completion invalidates evidence but grants no authority.
   - NewGame/MainMenu: completion only after the original boundary returns successfully.
   - exception/partial transition: leave domain closed or poisoned; never reopen optimistically.
5. Close TOCTOU between capability publication and hook availability. Uninstall/failure may not leave capability reported as complete.
6. Preserve current shutdown/quiescence architecture. Do not redesign `World`, transport workers or dispatcher destruction.

## Regression tests

At minimum cover: missing each individual hook; queued-but-not-committed hook; failed post-commit validation; duplicate install; unsupported runtime/binary; LoadGame request/completion pairing; unpaired load event; NewGame/MainMenu pre-transition ordering; exception/failed original; concurrent lease versus transition; reentrant invalidation; capability reset/uninstall; no reopening from stale completion ticket.

Add a deterministic test that shows old generation work cannot execute after each of the three transitions even when local quest/Form IDs are reused.

## Done when

- Production reports complete coverage only for an exact validated runtime/hook set.
- Every identity-changing path crosses the same owner/generation fence before the engine transition.
- Failures leave execution closed and tests prove no stale/reentrant escape.
- Canonical mutation remains disabled.
- Task branch is FULL GREEN on all four required jobs with exact Linux/Windows TPTests counts.

If a target/ABI cannot be proven, stop with the exact missing evidence; do not add a speculative hook.
