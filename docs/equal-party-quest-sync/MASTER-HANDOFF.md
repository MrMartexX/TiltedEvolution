# Equal-party quest sync P0 master handoff

Status: normative execution protocol

Read [CURRENT_STATUS.md](../project/CURRENT_STATUS.md) before every task. It is
the only location for the current integration SHA and live CI snapshot.

## Project

- Repository: `MrMartexX/TiltedEvolution`
- Integration branch: `feature/equal-party-quest-poc`
- Base branch: `dev`
- Pull request: `#1`

## Start protocol

Before changing code:

1. Live-check PR state, integration remote HEAD, mergeability and workflows.
2. Verify the last fully green accepted SHA and exact TPTests counts.
3. Verify the local checkout, ancestry and clean working tree.
4. Start a new narrow task branch from the current accepted integration HEAD.
5. Do not continue or merge an older task branch automatically.
6. Preserve unexpected user or parallel work.

## Architecture and safety

CampaignState belongs to the server campaign. The leader is an administrator,
not authoritative ownership. Local saves are independent player replicas.

The required runtime chain is:

```text
canonical server intent -> admission -> owner/generation-bound deferred work
-> consume-time revalidation -> compatibility -> quiescence
-> PowerLossDurable PreRepair checkpoint -> narrow mutation
-> postcondition verification -> commit or deterministic recovery
```

Until every preceding capability is proven:

- canonical `TESQuest::SetStage` stays disabled;
- aliases, quest objects, inventories and generic world state are not mutated;
- `TESObjectLoadedEvent` and pointer readiness remain evidence only;
- missing observers/capabilities fail closed;
- C++ exceptions do not cross Skyrim, SKSE or Papyrus ABI boundaries;
- ordinary solo saves stay outside the writable co-op replica tree.

Do not change the wire protocol unless existing identities are proven
insufficient and the architecture finding is reviewed first.

## Code-changing slice protocol

For one independent defect or task:

1. State the root cause or required invariant.
2. Implement the smallest coherent fix.
3. Add regression tests that would fail without it.
4. Run relevant local checks.
5. Commit and publish the narrow task branch.
6. Require Build Linux and Build Windows.
7. Require equal-party diagnostics Linux and Windows.
8. Record exact TPTests assertion/test-case counts for both platforms.
9. Inspect logs to prove required steps ran rather than being skipped.
10. Integrate only after all required gates pass.
11. Start the next independent task from the new accepted integration HEAD.

A corrective commit for the same slice is allowed. An unrelated fix must wait
or use an independent branch.

## Live proof rules

- Bind evidence to exact source SHA, artifact hash, Skyrim executable, SKSE,
  Address Library, mod profile and server build.
- Preserve timestamps and relevant client/server logs.
- Do not enable large crash dumps unless a specific investigation requires
  them.
- Do not overwrite normal saves or reuse production save data for destructive
  fault injection.
- One crash-free run is positive evidence, not proof of all lifecycle paths.

## P0 acceptance

`P0 CLOSED` is allowed only when one exact candidate has:

- current remote/integration/final SHA evidence;
- all four required CI checks and exact Linux/Windows TPTests counts;
- exact-runtime live evidence;
- lifecycle ownership and stale-work revalidation;
- Papyrus/Skyrim quiescence;
- compatibility profiles and complete verification postconditions;
- PowerLossDurable PreRepair capture;
- deterministic crash/restart recovery;
- two-client narrow mutation evidence;
- disconnect/reconnect/LoadGame/campaign-transition/failure matrix;
- an independent adversarial audit;
- shipping configuration that enables only reviewed transitions;
- no unresolved required P0 blocker.

Otherwise the report begins `P0 NOT CLOSED` and lists current/green/working
SHAs, proven facts, unknowns, the exact blocker and the next concrete action.
