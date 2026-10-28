# Task 14 — Validate lifecycle, crash and power-loss recovery live

Read `../MASTER-HANDOFF.md`. Start only after Task 13's controlled artifact and evidence are independently accepted. Follow the master protocol. Use disposable isolated profiles and coordinate any real machine power interruption with the user.

## Goal

Try to break the complete controlled pipeline at every meaningful boundary and prove it fails closed or recovers deterministically without corrupting ordinary saves or applying a canonical transition twice.

## Live lifecycle matrix

For a pending, checkpointed and post-attempt operation, exercise as applicable:

- disconnect/reconnect with same and changed player/session identity;
- party leave/rejoin and party recreation;
- campaign A→B with reused quest/Form IDs;
- LoadGame to another save and back;
- NewGame;
- return to Main Menu;
- location unload/reload and repeated `TESObjectLoadedEvent`;
- orderly client shutdown and server shutdown/restart.

For every case record whether work was rejected, requeued with fresh authority or recovered. Old authorization must never survive.

## Crash/fault matrix

Terminate the client abruptly after: request admission; deferred enqueue; readiness; checkpoint file writes; checkpoint publication; immediately before native dispatch; immediately after native return; during postcondition sampling; before client ACK; after ACK but before local retirement.

Also test server operation replay, disk full/write denial where safely simulated, corrupt/truncated checkpoint metadata, missing `.skse`, and stale server revision after restart.

## Power-interruption evidence

Ordinary process kill proves crash recovery, not power-loss durability. For the final durability claim, create a safe scripted checkpoint boundary and ask the user for one explicit restart/power-cut action only after all non-destructive tests pass. Do not power off the machine autonomously. After reboot, inspect on-disk evidence before launching mutation/recovery.

If a real power-cycle test is impractical, report P0 NOT CLOSED for the PowerLossDurable requirement rather than renaming weaker evidence.

## Performance/stability

Repeat the Dragonsreach/quest-progress scenario and extended movement/save/load play. Capture update-thread latency percentiles/max, compatibility-cache behavior, observer waits, queue depth and memory/thread stability. No 10–50-second hangs, launch crash or unbounded retry/log loop is acceptable.

## Acceptance rules

- At most one canonical native attempt per transaction.
- Stale work never executes after any lifecycle transition.
- No committed ACK without verified postconditions.
- Recovery selects only the exact campaign/profile/generation transaction checkpoint.
- Ordinary saves and unrelated quests remain unchanged.
- The server remains authoritative across reconnect/replay.

## Done when

- The full matrix has reproducible artifacts/logs and no unexplained result.
- Crash recovery and, if required, real power-loss durability are separately proven.
- Controlled activation remains bounded to the reviewed profile.
- Any code correction receives its own focused commit and full four-job CI with exact counts.
