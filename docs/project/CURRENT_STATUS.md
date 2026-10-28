# Current project status

Status: consolidated integration; P0 NOT CLOSED

Last verified: **2026-10-27**

## Latest accepted integration & local Task 08/09 progress

- **Integration baseline (`feature/equal-party-quest-poc`)**: `769e7fef2933f82f91698bf8828f605195bae3be` (integrates Task 08/09 pre-repair authorization, live recovery binding, and Windows NTFS durable recovery through `769e7fef`).
- **Active local Task 10 stack (`aih/task10-production-dry-run-pipeline`)**:
  - Connects the client canonical update pipeline end-to-end in strict dry-run mode without invoking native mutation (`TESQuest::SetStage` remains uncalled).
  - Integrates `PartyQuestDeferredWorldQueue` directly into `PartyQuestRuntimeOwner` to eliminate runner shadow queues and atomically flush deferred work on lifecycle boundaries.
  - Implements bounded deferred-world readiness processing and `EvaluateDryRun` reporting explicit deterministic outcomes (`WouldApply`, `Stale`, `Unsupported`, `RetryableBusy`, `Timeout`, `RecoveryBlocked`, `Invalid`). (PR #19 merged tested candidate `dd9cfca39129f6626f9d1d1273197f8de7ac0153`, resolving the two Windows `PartyQuestSkyrimNativeLoadBridgeCoordinator` cancellation regressions from `4c85e209` with all four CI jobs green: Build Linux `36052130392`, Build Windows `36052130495`, Diagnostics Linux `36052126500` at `283539 assertions / 888 cases`, Diagnostics Windows `36052126500` at `284312 assertions / 929 cases`).
- **Active local Task 08/09 stack (`aih/task08-runtime-authorization-integration`)**:
  - Completes Windows NTFS durable directory-tree promotion (`NtFlushBuffersFileEx`), `CopyFileDurably`, `RemoveFileDurably`, and `RemoveEmptyDirectoryDurably` in `PartyQuestStableStorage`.
  - Routes both crash recovery (`PartyQuestRuntimeRecoveryCoordinator::Recover`) and active in-process live recovery (`PartyQuestRuntimeLiveRecoveryCoordinator::ResolveLiveRecovery`) on Linux/POSIX and Windows NTFS through `PartyQuestRuntimeRestoreAttemptStore`, `PartyQuestReplicaDurableRestorePreparation`, and `PartyQuestReplicaDurableRestoreExecutor` (`PowerLossDurable`), separating stable `RuntimeTransactionId` from monotonic `RestoreAttemptId` tombstones.
  - Binds immutable `pre_repair_commit.bin` checkpoint authorization to both crash recovery and active in-process live recovery before any restore plan executes, while preserving `PartyQuestSaveGuard` and the runtime barrier on any missing, corrupt, or mismatched authorization record.
  - Hardens `.github/workflows/equal-party-poc-diagnostics.yml` (`build-poc-windows`) to echo the `TPTests` summary tail into the permanent GitHub Actions job log in addition to the 7-day diagnostic artifact.
  - Local Windows x64 `releasedbg` verification (`manage.py check --profile cpp`): all product targets built (`SkyrimTogetherClient`, `SkyrimTogetherServer`, `SkyrimImmersiveLauncher`, `PartyQuestCompatibilityAnalyzer`) and `TPTests` passed with **286,666 assertions in 974 test cases**.
  - Tasks 07, 08, and 09 remain **not accepted** until fresh four-job CI on the candidate branch, exact-runtime 1.6.1170 live/fault-injection evidence, and physical power-loss validation are completed.

## Consolidation accepted on 2026-09-24 (supersedes historical snapshot below)

PR #18 merged the load-bridge stack through `311e5322`, PR #10 inventory
recreation safety, PR #12 checked inventory arithmetic, PR #8 ecosystem
documentation, PR #9 research evidence, and additional runtime owner quiescence
tests into `feature/equal-party-quest-poc`.

- Tested candidate: `ac0ae93a5c8a90c02cab1d220293d02cc0ea6160`.
- Integration merge: `a75903ab65e4e17dd6c11b1875742d12ab23f953`.
- The merge tree was verified identical to the tested candidate tree.
- This subsequent status update changes documentation only; it does not claim
  fresh four-job acceptance for a later documentation commit.

| Validation on the tested candidate | Result | Exact TPTests evidence |
|---|---|---|
| Build Linux, run 36044892449 | Success; actual build executed | Not a test-run job |
| Build Windows, run 36044892796 | Success; client/UI built and packaged | Not a test-run job |
| Diagnostics Linux, run 36044887056 | Success | 283511 assertions / 888 cases |
| Diagnostics Windows, run 36044887056 | Success | 284284 assertions / 929 cases |

Local MSVC release TPTests also passed: 284273 assertions / 929 cases. Two
existing symlink tests reported unavailable local symlink-creation privilege;
the hosted Windows result above is separate evidence.

The sections below retain historical acceptance evidence only.

Before consolidation the integration HEAD was `d8f1344b`, and the last verified
load-bridge baseline with all required workflows successful was `5001c8b2`.
The playable artifact at `311e5322` alone is not proof of all four CI gates.

Canonical mutation remains disabled. Load diagnostics and tested ownership
primitives do not establish complete engine save/load, recovery, durability,
or two-client live acceptance. Task 07/08 and P0 remain open.

The only unaccepted follow-up is preserved in
`codex/task07-load-bridge-followup` at
`4c85e2098bd8f78872e951781cb567da3be018b5`. Its Windows CI failed two
`PartyQuestSkyrimNativeLoadBridgeCoordinator` cancellation regressions (reserved
request cancellation and stale-attempt isolation after cancellation). It is not
part of the accepted consolidation and requires correction and independent
validation before integration. Intermediate branches were removed only after
ancestry/patch-equivalence review or verification that their sole unique change
was a temporary CI trigger.

Keep `dev` as the upstream base and `feature/equal-party-quest-poc` as the main
working branch. PR #1 remains draft. The pre-consolidation local reserve patch
is preserved in Git stash `preserved-before-2026-09-24-consolidation`; complete
pre-cleanup Git bundles are retained outside the checkout on drive D.

This is the only project-wide status source. Re-check GitHub and the local
checkout before changing code; no SHA in this file is permanent.

## Historical accepted integration baseline

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

## Historical ordered task status

| Task | State | Evidence or next gate |
|---:|---|---|
| 01 Lifecycle coverage | Accepted | Integrated before the current baseline |
| 02 Production runtime bootstrap | Accepted | Integrated before the current baseline |
| 03 Papyrus observer 1.6.1170 | Accepted | Integrated before the current baseline |
| 04 Offline compatibility analyzer | Accepted | Merged through PR #4 |
| 05 Compatibility admission | Accepted | Merged through PR #5 |
| 06 Verification envelope | Accepted | Merged through PR #6; current integration HEAD |
| 07 Skyrim/SKSE async save contract | Local implementation | Load-bridge follow-up merged in `96a0877b` (PR #19); full Task 07 still requires exact-runtime live/fault acceptance |
| 08 Power-loss-durable checkpoint | Local implementation | Cross-platform NTFS/POSIX durability + immutable `pre_repair_commit.bin` authorization verified locally (`286,666 assertions / 974 cases`); awaits four-job CI and physical power-loss acceptance |
| 09 Deterministic recovery | Local implementation | Crash and live recovery strong restore-attempt routing + authorization binding verified locally; awaits Task 08 acceptance and live crash validation |
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
