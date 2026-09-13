# Current project status

Status: live-verified snapshot

Last verified: **2026-09-13**

This is the only project-wide status source. Re-check GitHub and the local
checkout before changing code; no SHA in this file is permanent.

## Accepted integration baseline

- Repository: `MrMartexX/TiltedEvolution`
- Pull request: `#1`
- Integration branch: `feature/equal-party-quest-poc`
- Base branch: `dev`
- Accepted integration HEAD: `d8f1344b728fe0b66d594f5d8705ca850b1a5254`
- PR state: open, draft, mergeable
- Merge-state status: clean

Required checks on that exact SHA:

| Validation | Result | TPTests evidence |
|---|---:|---:|
| Build Linux | Success | Not a TPTests job |
| Build Windows | Success | Not a TPTests job |
| Equal-party diagnostics Linux | Success | 166547 assertions / 613 test cases |
| Equal-party diagnostics Windows | Success | 166538 assertions / 613 test cases |

The integration HEAD changed only by merging the documentation organization
slice in PR #7. All four checks above completed successfully on the exact merge
HEAD.

## Ordered task status

| Task | State | Evidence or next gate |
|---:|---|---|
| 01 Lifecycle coverage | Accepted | Integrated before the current baseline |
| 02 Production runtime bootstrap | Accepted | Integrated before the current baseline |
| 03 Papyrus observer 1.6.1170 | Accepted | Integrated before the current baseline |
| 04 Offline compatibility analyzer | Accepted | Merged through PR #4 |
| 05 Compatibility admission | Accepted | Merged through PR #5 |
| 06 Verification envelope | Accepted | Merged through PR #6; current integration HEAD |
| 07 Skyrim/SKSE async save contract | Local implementation | Local head `7be2562b`; requires independent review, four CI jobs and exact-runtime live/fault evidence |
| 08 Power-loss-durable checkpoint | Local implementation | Local head `6dbb27ec`; depends on accepted Task 07 and unresolved Windows durability proof |
| 09 Deterministic recovery | Planned | Start only after Task 08 acceptance |
| 10 Production dry-run pipeline | Planned | Requires Tasks 01–09 |
| 11 Divergent-save reconciliation | Planned | Requires production dry-run observations |
| 12 Reviewed quest profiles | Planned | Requires accepted reconciliation model |
| 13 Narrow mutation/two-client validation | Planned | Requires Tasks 01–12; disabled by default |
| 14 Failure/recovery live validation | Planned | Requires the exact Task 13 artifact |
| 15 Final adversarial audit | Planned | Requires Tasks 01–14 accepted on one candidate |

## Local-only supporting work

The following work is not part of the accepted integration branch:

- `diagnostics/interaction-save-state` at `e85a8f5b`: read-only interaction,
  inventory, dialogue, death and save-state tracing plus an offline analyzer;
- a live evidence trail identifying automatic naked-NPC `ResetInventory` as an
  item-duplication source;
- a server-authoritative follower design concept.

The diagnostic implementation is stacked on local Task 08. It must be re-sliced
onto the then-current accepted integration baseline and validated separately.

## External intake status

The dated [upstream and ecosystem review](../research/UPSTREAM_ECOSYSTEM_REVIEW_2026-09-13.md)
compared this integration HEAD with official `tiltedphoques/TiltedEvolution`
`dev` at `8a3cec96c4955193df9a0f5e33e75b5364e63dd1`.

- The branches share `9d81ef07d68e4bb2bd94fca246e798a564b7fb92`.
- Integration has 1,012 unique commits; official upstream has 21 unique commits.
- Upstream's merged actor ownership epochs and follow-up stale-owner rejection
  are high-priority intake candidates, not accepted code in this fork.
- Open object-lifecycle, jail-container, leveled-NPC and dialogue work supplies
  useful designs/tests but requires local adaptation and live evidence.
- A wholesale upstream merge is forbidden because it would cross wire,
  lifecycle, runtime and P0 authority boundaries at once.

## Current safety boundary

- Canonical `TESQuest::SetStage` is disabled.
- `AllowsNativeRuntimeMutation()` remains false.
- Alias, quest-object, inventory and generic world mutation are not authorized
  by the quest-sync pipeline.
- Reference readiness and `TESObjectLoadedEvent` are evidence only.
- Unknown compatibility, observer, ownership, generation or recovery state
  fails closed.
- Local `.ess`/`.skse` files remain player replicas, not server authority.

## Open critical blockers

1. Task 07 lacks accepted CI and exact 1.6.1170 live/fault-injection proof.
2. Task 08 lacks accepted Windows power-loss durability semantics.
3. Crash/restart recovery is not connected through the final strong production
   persistence domain.
4. The production dry-run chain is not complete end to end.
5. Divergent-save policy and reviewed production quest profiles are absent.
6. No narrow mutation is enabled or proven with two clients.
7. The final crash/disconnect/load/reconnect/power-loss matrix is absent.
8. The independent adversarial P0 audit has not occurred.
9. The live gameplay baseline still contains the old optimistic actor ownership
   and item-creating naked-NPC workaround; it must be replaced or independently
   made fail-closed before broad P0 live acceptance.

## Next ordered work

1. Review, validate and integrate Task 07.
2. Complete Windows durability, validate and integrate Task 08.
3. Reconcile upstream runtime support in a narrow exact-binary slice.
4. Port and harden the merged upstream ownership epoch stack and its stale-owner
   follow-ups; this must remove the item-creating `ResetInventory` repair.
5. Re-slice and validate the observational diagnostic layer.
6. Continue Tasks 09–15 in order, evaluating open upstream object/dialogue work
   only as separate reviewed slices.

Current decision: **P0 NOT CLOSED**.
