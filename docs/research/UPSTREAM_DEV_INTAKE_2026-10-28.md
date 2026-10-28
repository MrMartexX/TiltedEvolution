# Upstream dev intake review — 2026-10-28

Status: Evidence snapshot and preparation plan; no integration acceptance.

## Reviewed baseline and method

This report reviews the upstream delta and a virtual three-way merge. It does
not contain a resolved merge candidate or claim that the combined code builds.

| Reference | Exact SHA / observation |
|---|---|
| Fork dev / common ancestor | `9d81ef07d68e4bb2bd94fca246e798a564b7fb92` |
| Remote integration branch | `99b4d0803810c22d30f14365b5c94d7fc5dd290c` |
| Upstream dev | `25a0049a00900b8714e105f795380caf33678012` |
| Integration-only / upstream-only commits | 1,139 / 107 |
| Upstream non-merge / merge commits | 99 / 8 |
| Net upstream delta from common ancestor | 354 files; +6,096 / -16,471 text lines |
| Files changed on both sides | 34 |
| Textually conflicting files / conflict regions | 20 / 36 |
| Overlapping files auto-merged by Git | 14 |
| Virtual merge tree, containing unresolved markers | `819a145340f69aac1688d31a30ac181630093f93` |

Live GitHub PR #1 is OPEN, DRAFT, base dev, head feature/equal-party-quest-poc.
The connector initially returned mergeable=false; the subsequent GitHub CLI
query returned MERGEABLE / CLEAN. The latter is the refreshed observation.
These refer to PR #1 against the fork's old dev, not against the new upstream.
They do not contradict the 20 conflicts observed in the upstream simulation.

The old local integration checkout is clean at `92a15569`, ten commits behind
the reviewed remote integration tip. Analysis uses a separate detached worktree
at the current remote tip:

`D:\Codex\Workspaces\2026-10-28\upstream-dev-review`

The existing two-file local follow-up in the old authorization worktree was
preserved. No working branch was reset, fast-forwarded, merged or pushed.
The old remote authorization branch was absent from live ls-remote; its cached
tracking ref is not evidence that it still exists.

All 107 commit subjects and the full file/overlap/conflict inventory are
recorded in the [machine-readable evidence](UPSTREAM_DEV_INTAKE_2026-10-28.json).
Review covered the history, final subsystem deltas and risk-bearing call paths.
It was a source review: no IDA/Ghidra binary verification, merged build, game
launch, two-client run or power-interruption experiment was performed.
Commit titles and upstream comments are not treated as runtime proof.

## What changed and what to do with it

| Area | Evidence / change | Intake decision and remaining proof |
|---|---|---|
| Actor ownership | #887, `1f488f6f`, `1660eb0c`: versioned server grants, ExpectedOwnershipEpoch claims, stale-owner rejection, explicit release/decline, notification to previous owner even out of range | Adapt as one connected client/server/message change. Preserve fork lifecycle fencing; ownership epochs are actor leases, not campaign/runtime transaction identities. |
| Stale assignment duplication | #906, `8f08d405`: owners stop applying stale assignment snapshots that respawn/re-roll live actors | Adopt with ownership stack. Exercise death while assignment is in flight, lost assignment/cancel, handoff and rediscovery. |
| Leveled NPC identity | Early July series; #892; #899 `10e64706`: relay the chosen base, first server pick wins, reconstruct using original base/template flags, asynchronous disable/rebuild/enable | Adapt. Include DiscoveryService retention, ActorExtension reconciliation, template APIs, extra data and garbage collection together. It is not merely a cosmetic mesh fix. |
| Inventory | `42a518a0`: non-owner pickpocket/loot updates relay; actor inventory/equipment carry OwnershipEpoch; upstream removed naked-NPC ResetInventory loop | Adopt epoch/range validation while preserving fork overflow checks and anti-recreation policy/tests. Epoch does not provide an idempotent item-transfer transaction. |
| Player-specific containers | #893, `59669245`: faction jail belongings and stolen-goods containers excluded from object sync | Adopt with TESFaction/ModManager layout changes. Test jail/quest confiscation with two independent inventories. |
| Dialogue and scenes | #896; #854 `ea0f6c39`; #928 `5a72cdcd`: dialogue from interacting non-owner, scene/phase identities, skip lines voiced by local scene, replay lines when local phase is blocked by remote package | Adapt with message and scene declarations. Source changes SpeakSound hook return type bool -> float; verify ABI on exact binaries and test courier/follower/guard/cutscene behavior. This does not establish an exclusive interaction lease. |
| Legacy quest progression | #848 `e9214a87`: server per-party stage dedup; leader holds member stages until local scene conditions permit; waits for stage-item fragments | Do not activate mechanically. See findings below. Preserve canonical dry-run pipeline and legacy/canonical separation. A scene readiness check is not canonical mutation authority. |
| Physics and animation safety | #901 `fbf72883`: valid remote controller timing; #917 `115b5019`: project-specific animation descriptors and safe word variables; #920 `390071ca`: smart-pointer lifetime for animation manager | Adopt the related structs, functions and guard types as a coherent set; test combat, transformed/leveled creatures and world transitions. |
| ExtraDataList freeze | #937 `25a0049a`: replace mismatched recursive lock operations with engine read/write-lock functions and matching guards | High-priority intake. Requires all BSAtomic/ExtraDataList/animation lock dependencies and exact-runtime verification, not a lone unlock change. |
| Remote playback / network | #921: adaptive delay, bounded interpolation/extrapolation, out-of-order movement handling, reliable action stream separated from unreliable movement | Adopt as a connected transport/protocol/submodule slice; retain upstream PlaybackClock test. Review action epoch/bounds gap before stronger safety claims. |
| Respawn / controls | `2e52a704`: steady-clock death timers; #875/`97dc28ad` and #902: per-cell safe respawn overrides; #916: debug restore-controls helper | Adopt timing and overrides with death-system tests; debug controls restoration is diagnostic, not repair proof for a damaged save. |
| Magic / map crashes | #894: suppress duplicate summons from staves; #907: initialize DesiredTarget; #930: remove unresolved NPC spell entries; #934: reject waypoint without worldspace/cell; `69fe8e67`: do not relay permanent scripted slow-time ability | Adopt targeted fixes. Test missing-mod forms and summon count; preserve missing-mod partial mapping semantics. |
| Runtime / launcher | #885; #930 `e1de917c`: 1.7.104 default plus explicit 1.6.1170 build, GameVM offsets, early SKSE startup, Address Library v5, KiLoader dependency search path, SteamStub zero-section handling | Reconcile with stronger fork versions rather than replacing them; see runtime findings. Upstream source support is not local live acceptance. |
| UI / public parties | Password masking, party hint/play guide, public-server autojoin disabled, debug stats corner, Swedish/Ukrainian translations, invitation reset on disconnect | Adopt with frontend build/manual UI smoke check. The stats positioning change is relevant to the prior visual issue but is not proof of fixing that exact report. |
| Content | #855 `9df6c400`: SkyrimTogetherQuestPatches.esp Companions C02 induction patch | Review and ship intentionally. The ESP changes the compatibility environment and requires analyzer/profile refresh; two clients must have the same accepted content. Binary/plugin semantics were not inspected in an editor here. |
| Toolchain / distribution | xmake 3.1.0; tiltedcore 0.2.9; reusable Windows/Linux release flows; Linux amd64/arm64; server --generate-config; packaging fix; Nix shell simplification | Adapt while keeping fork's unconditional diagnostics and exact test summaries. Upgrade build and diagnostics configurations together; keep runtime artifacts distinguishable. |
| Cleanup / guidance | Remove unused admin protocol, old one_ui frontend and animation experiments; new AGENTS.md and addrlib.py | Adopt cleanup after checking references. Much of the deletion count is obsolete UI, including an 11,899-line lockfile. Adapt agent guidance to the fork's authority/wire requirements. |

### Submodule provenance

- TiltedConnect: `293fab99288b7414bd129ace25ae018114cefb2a` ->
  `e6f5759364a64462e717db1153781a7eae26a13e`. Reviewed history has one new
  commit: no-Nagle unreliable sends and clock synchronization after long frames.
  Transport uses the new packet flags and receive-time API, so the old submodule
  cannot be retained while importing those call sites.
- TiltedUI: `a2b9a671626d6489e6e2d9ee02adf61d7d9673dd` ->
  `0bec46b174d730832cbe0a10e8162bf6b0d3d66b`: do-not-de-elevate switch and its
  merge. Verify the overlay under the actual launcher/MO2 privilege context.
- Root LICENSE blobs are identical on both reviewed tips:
  `048735d3e6cb1875e759425df17729a4390b2c77` (GPL-3.0-or-later text with the
  existing launcher exception/attribution). Preserve attribution, component
  notices and the pinned dependency licenses. This is not a new full dependency
  license audit.

## Semantic findings that text merging does not solve

### 1. Held quest stages are a new deferred execution path

Upstream QuestService owns m_heldStages, keyed only by local quest FormID.
It stores sorted stage numbers and pending stage-item numbers. OnUpdate
releases them based on scene conditions; ApplyHeldStages calls ScriptSetStage.

Disconnect/party-leave clear the map. StageItemDone queues a lambda capturing
this and copying questFormId/stage from an engine callback. The new sinks include
stage-item, scene, action, phase and topic-info events.

The stored item has no campaign/session/runtime-generation/transaction/revision
envelope. The new methods do not themselves establish the fork's lifecycle lease
or callback quiescence contract. This is a source-level missing proof, not a
claim that every upstream session necessarily exhibits a stale-work crash.

Resolution: preserve the canonical inbox -> owner -> deferred queue -> dry-run
gate already in the fork. Keep upstream held-stage execution disconnected from
canonical authority. For legacy adoption, bind its work to the relevant owner
and lifecycle generation, invalidate on every required transition, protect
consume-time execution, and register/unregister each sink through the reviewed
teardown primitive. Review that as a bounded behavioral change.

The existing fork's legacy OnQuestUpdate already calls ScriptSetStage. The
canonical PartyQuestSkyrimStageMutationExecutor is separately non-mutating.
Therefore a scan for any SetStage/ScriptSetStage call anywhere is not a valid
test of the canonical safety boundary.

### 2. Runtime-specific GameVM layout affects unchanged P0 code

Upstream GameVM has virtualMachine/inactive at 0x200/0x680 for
TP_SKYRIM_1_6_1170, and 0x210/0x690 otherwise. The fork's Papyrus observer reads
GameVM::Get()->virtualMachine but has separately reviewed runtime identity,
vtable, queue offsets, raw spinlocks and ingress hooks.

The header auto-merges without a text conflict. A build that silently uses
the default latest option can change pointer interpretation underneath those
unchanged observer files. Preserve the observer's exact-runtime rejection.
Build and package 1.6.1170 explicitly for the installed test environment;
review newer-runtime observer support separately. The upstream 1.6.1179 string
is not automatic approval of the fork's observer for that GOG binary.

Keep fork MainMenu reset fencing/thread observation in SkyrimVM64.cpp while
using the selected VM layout. Do not globally substitute the fork's private
RawSpinLock code merely because upstream replaced unrelated engine lock types.

### 3. Fork Address Library parsing is already stronger

The fork VersionDb delegates to SkyrimAddressLibraryDatabaseParser: bounded
input, exact version/module/pointer width, supported format/namespace and
transactional publication. Its parser already accepts formats 1, 2 and 5;
the runtime loader still rejects an unreviewed SE ID namespace.

Upstream's new LoadV2/LoadV5 stream parser is not an improvement over this
implementation. Keep the fork parser and its tests. Do not rename database
files, prefer a variant by filename alone, or infer executable identity solely
from a numeric game version.

### 4. Inventory validation must precede one checked mutation

The unresolved virtual server inventory merge has both the fork's checked
AddOrRemoveEntry and upstream's unconditional AddOrRemoveEntry around the
conflict. Resolving only the markers could apply a delta twice.

Correct order: find entity -> validate sender/epoch/range -> call checked
AddOrRemoveEntry once -> abort on failure -> relay accepted result.
PlayerService gold-loss handling must preserve its corresponding checked
arithmetic branch while adding ownership epoch to a successful notification.
Keep both existing inventory arithmetic and naked-NPC policy regressions.

The fork already prevents appearance-only inventory recreation. Upstream
removing RunNakedNPCBugChecks is aligned with that safety rule; it is not a
newly proven fix exclusive to either branch.

### 5. New NPC base-rebuild queue needs lifecycle review

m_pendingLeveledConforms stores local actor FormID -> local pick FormID.
ProcessLeveledConforms does late lookup, waits around loading/3D, disables the
actor asynchronously, rebuilds its base and re-enables it. It clears on
disconnect/removal, with a cleanup re-enable for a pending disable.

Neither this item nor the consumer carries an explicit accepted ownership epoch
or runtime generation. Before admission, prove how LoadGame, handoff, deletion
and FormID reuse revoke it. These are real actor/base changes outside the
canonical quest pipeline and cannot be authorized by reference readiness alone.

### 6. The new action stream is not fully epoch-fenced

RequestActionEvents/NotifyActionEvents contain server ID -> action vectors.
Unlike inventory messages, they do not carry an ownership epoch. The server
checks current owner through OwnerView; the client selects RemoteComponent by
server ID. Deserialization sizes vectors directly from incoming counts.

This is a review gap for delayed actions across ownership changes/reuse and for
resource bounds, not proof of a demonstrated exploit. Decide and test bounded
decode plus stale-action handling before claiming that the upstream ownership
upgrade closes every replay scenario. Do not silently redesign the protocol
during a mechanical conflict resolution.

### 7. Automatic merges still need a safety review

The virtual tree changes no files selected by PartyQuest*, party_quest_*,
QuestSnapshotCollector, NakedNpcInventoryRepairPolicy or InventoryDelta.
That preserves their source bytes, but their called types, hooks, ABI,
dependency versions and lifecycle events still change.

Auto-merged SteamCeg.cpp retains the corrected fork header/decryption path and
adds upstream zero-section early return. Keep fork signature/OEP/NoEncryption
validation and post-decryption/AEP checks; test combinations of zero section
metadata and encryption flags instead of assuming both early exits equivalent.

TESQuest's stages changes from GameList<Stage> to GameValueList<Stage*>.
Collectors, diagnostics, ContainsStage and topology hashing use this field even
though their own source files do not conflict. Recheck observed stage lists,
empty-list handling, hash stability and reviewed profile evidence on exact
content/runtime. Keep aliases and existing offset/sizeof assertions when
combining scene/stage declarations.

### 8. Upstream instructions cannot replace fork architecture

New upstream AGENTS.md describes a leader/client-authoritative relay model and
allows arbitrary wire reordering because endpoints require matching BUILD_COMMIT.
The fork targets server-owned canonical campaigns and versioned PartyQuest
messages. Adapt the guide to that architecture and preserve the canonical
mutation boundary. No instruction file grants runtime or mutation authority.

The existing CURRENT_STATUS external-intake section forbids a wholesale
unreviewed merge. This preparation does not change that rule or claim approval.
A reviewed integration candidate needs an explicit intake decision and updated
status/provenance; old historical status prose must not be treated as current
evidence.

## Conflict resolution map

The virtual tree is only diagnostic; it contains conflict markers and must
never be built, committed as a candidate, or published.

| File | Conflict regions | Required resolution |
|---|---:|---|
| `.github/workflows/linux.yml` | 1 | Retain fork scope/test guarantees; add upstream build/package logic and separate arm64 coverage. |
| `.github/workflows/windows-playable-build.yml` | 1 | Preserve manual dispatch/playable artifact flow; add explicit runtime artifact matrix. |
| `.github/workflows/windows.yml` | 6 | Keep fork validation and logs; integrate runtime option and packaging without scope-skipping builds. |
| `Code/client/Games/Skyrim/Forms/TESQuest.h` | 1 | Union reviewed upstream stage/scene declarations with fork aliases and layout asserts; revalidate fingerprints. |
| `Code/client/ScriptExtender.cpp` | 1 | Keep bounded, exception-contained fork loader; coordinate LoadScriptExtender rename and pre-entry call. |
| `Code/client/Services/Generic/QuestService.cpp` | 3 | Keep canonical protocol/dry-run/fences; do not activate upstream held-stage executor by mechanical merge. |
| `Code/client/Services/QuestService.h` | 4 | Preserve runtime/session/cache ownership and scoped teardown; any new sinks need matching lifetime handling. |
| `Code/client/SkyrimVM64.cpp` | 2 | Keep MainMenu generation fence and thread observation; reconcile runtime-specific GameVM signatures/layout. |
| `Code/client/TiltedOnlineApp.cpp` | 1 | Keep P0 include/bootstrap/shutdown logic; adopt per-instance logs. |
| `Code/client/VersionDb.h` | 2 | Keep strict tested database parser/publication; do not replace it with upstream stream decoder. |
| `Code/client/main.cpp` | 3 | Keep pre/post hook validation and pre-entry SKSE initialization; add explicit runtime admission. |
| `Code/client/xmake.lua` | 1 | Retain fork link/source requirements; add upstream runtime define; remove redundant tiltedcore declaration only after root registration. |
| `Code/encoding/Messages/ClientMessageFactory.h` | 1 | Union fork PartyQuest visitors with upstream messages/removals; retain registration tests. |
| `Code/encoding/Messages/ServerMessageFactory.h` | 2 | Union fork PartyQuest visitors with upstream messages/removals; retain registration tests. |
| `Code/encoding/Opcodes.h` | 2 | Coordinate enum/factory changes with both endpoints; preserve PartyQuest opcodes and max bounds. |
| `Code/server/Services/InventoryService.cpp` | 1 | Validate sender/epoch/range first, apply checked AddOrRemoveEntry exactly once, broadcast only on success. |
| `Code/server/Services/PlayerService.cpp` | 1 | Keep checked gold delta rejection; populate OwnershipEpoch only for a successful change. |
| `Code/server/Services/QuestService.cpp` | 1 | Keep canonical transaction/journal path; isolate legacy per-party stage suppression and reset semantics. |
| `Code/tests/xmake.lua` | 1 | Union PlaybackClock source with Windows native bridge sources/resources/syslinks. |
| `xmake.lua` | 1 | Keep fork dependency policy; coordinate tiltedcore 0.2.9 and skyrim_version; verify both platforms. |

These 14 overlapping files auto-merge, requiring the semantic checks above:

- `.github/workflows/docker-server-image.yml`
- `Code/client/Games/Animation.cpp`
- `Code/client/Games/Skyrim/Actor.cpp`
- `Code/client/Games/Skyrim/Misc/BSScript.h`
- `Code/client/Services/DiscoveryService.h`
- `Code/client/Services/Generic/DiscoveryService.cpp`
- `Code/client/Services/Generic/InventoryService.cpp`
- `Code/client/Services/Generic/ObjectService.cpp`
- `Code/client/Services/Generic/PartyService.cpp`
- `Code/client/Services/Generic/TransportService.cpp`
- `Code/client/Services/InventoryService.h`
- `Code/immersive_launcher/steam/SteamCeg.cpp`
- `Code/xmake.lua`
- `README.md`

## CI evidence observed during this review

- Current integration `99b4d080`: PR statusCheckRollup empty; exact-SHA workflow
  run query returned zero runs. This is unverified code, not a green baseline.
- Most recent integration SHA in the fetched run history with all named
  workflows successful: `96a0877b5ebb8943ff083a0ff7f4a7766d257819`.
  Build Linux runs 36309824753 / 36327006094; Build Windows
  36309824749 / 36327007696; diagnostics run 36309824752.
- Diagnostics Linux job 108593485333 executed Build quest state tests and Run
  quest state tests successfully; permanent log reports **284,263 assertions
  in 909 test cases**.
- Diagnostics Windows job 108593485416 completed its build/test command step
  successfully. The old workflow redirected the TPTests log into an artifact;
  exact Windows counts were not recovered from the permanent log in this
  review. No strict FULL GREEN assertion with exact cross-platform counts is
  made.
- Exact upstream tip workflow query also returned zero runs; do not infer
  current upstream binary acceptance from upstream ancestry or commit titles.

Sources: [PR #1](https://github.com/MrMartexX/TiltedEvolution/pull/1),
[diagnostics](https://github.com/MrMartexX/TiltedEvolution/actions/runs/36309824752),
[Linux build](https://github.com/MrMartexX/TiltedEvolution/actions/runs/36327006094),
[Windows build](https://github.com/MrMartexX/TiltedEvolution/actions/runs/36327007696),
[upstream source](https://github.com/tiltedphoques/TiltedEvolution/tree/25a0049a00900b8714e105f795380caf33678012).

## Concrete implementation and acceptance order

One integration candidate can contain the upstream history and deliberate
conflict resolutions, but it needs checkpoints in the work below. Twenty
conflicting files are not twenty separate releases, and 107 commits are not
107 independent features.

1. Refresh and pin both heads again; retain this report as historical evidence
   if either changes. Validate the latest integration baseline on its own,
   recovering exact Linux/Windows TPTests summaries and actual build execution.
2. Create an isolated intake branch from that integration SHA. Merge the pinned
   upstream dev there. Preserve full history/provenance. Do not rebase the long
   integration history or move the existing dev prematurely.
3. Resolve build/runtime/loader declarations, message factories and enums,
   then ownership/inventory dependencies. Preserve all P0 guards and checked
   inventory deltas. Retain the tested source sets in TPTests.
4. Reconcile TESQuest and GameVM ABI-sensitive declarations with exact-runtime
   evidence. Keep canonical stage execution physically absent. Give new
   deferred/held paths explicit lifecycle handling or leave those behaviors
   unadmitted pending their own slice. Do not guess offsets to make a build pass.
5. Refresh compatibility analyzer outputs, topology evidence and reviewed
   profiles against the intended content set, including the new quest ESP.
   A profile that does not match must remain rejected; never relax a digest
   merely to regain admission.
6. Run the matrix below on the final candidate. Review the resulting diff
   against **both parents**, not only upstream. Ensure no runtime bridge,
   journal/recovery, tests or scope filters disappeared in resolution.
7. Only after green code gates and appropriate runtime evidence, integrate the
   tested candidate and update CURRENT_STATUS/task evidence. Fast-forward fork
   dev to the reviewed upstream tip as a coordinated subsequent step and
   refresh PR #1's new base comparison; changing dev first would immediately
   change PR #1's mergeability/diff while implementation is still unvalidated.

Existing unvalidated Task 08–12 integration work remains subject to its own
acceptance gates. Upstream intake does not close save isolation, durability,
recovery, two-client canonical mutation or the final P0 audit.

### Required automated checks

- Linux x64: actual server/shared build and complete TPTests; Linux arm64 build
  if the upstream distribution matrix is adopted.
- Windows x64: client, launcher, server, compatibility analyzer and complete
  TPTests, for explicit 1.6.1170 and latest client configurations.
- Equal Party diagnostics Linux and Windows must still execute full TPTests
  unconditionally; preserve useful permanent test summaries and exact counts.
- New upstream [client.playback_clock] test (one TEST_CASE / ten SECTIONs),
  protocol roundtrips/factories including PartyQuest opcodes, inventory
  arithmetic/recreation policy, Address Library parser, SteamStub/decryption,
  runtime lifecycle/Papyrus/bridge, checkpoint/recovery, dry-run, divergent
  saves and reviewed profile suites.
- Regression coverage for chosen adaptations: stale ownership during queued
  inventory/action/NPC-base work, cancel/handoff, duplicate inventory delta
  prevention, held quest readiness with stale generation, and teardown of
  newly registered sinks.
- UI production build; distribution contains matching client/server versions,
  intended ESP/scripts and clearly runtime-labeled binaries.
- Documentation topology/link validation and git diff --check.

Example intended commands after the runtime option has been reconciled:

```text
#Windows: separate worktrees/build outputs per runtime
xmake config --plat=windows --arch=x64 --mode=releasedbg --skyrim_version=1.6.1170 --yes -vD
xmake -y SkyrimTogetherClient
xmake -y SkyrimImmersiveLauncher
xmake -y SkyrimTogetherServer
xmake -y PartyQuestCompatibilityAnalyzer
xmake -y TPTests
xmake run TPTests "[client.playback_clock]"
xmake run TPTests
#Repeat in the latest-runtime build worktree with --skyrim_version=latest.

#Linux
xmake config --arch=x64 --mode=debug --yes -vD
xmake -y SkyrimTogetherServer
xmake -y TPTests
xmake run TPTests

#UI / docs
pnpm --prefix Code/skyrim_ui install
pnpm --prefix Code/skyrim_ui deploy:production
python Tools/documentation/validate_docs.py
```

These commands describe the future merged build. They were not run as merged
code validation during this source-only preparation.

### Required live checks once a candidate exists

Use the isolated Skyrim 1.6.1170 installation first, with matching server/client
artifact identities and separate test saves/logs. Verify menu/load/reload,
disconnect/reconnect, party leave/join, MainMenu/NewGame, NPC ownership across
cell travel, leveled actors/dragons, follower trading, courier/guard dialogue,
repeat pickpocketing without inventory recreation, death/respawn, summons,
combat and scene voice/subtitles with two clients.

Check that dry-run never calls the canonical native mutation adapter, that
stale pending work cannot cross load/session transitions, and that unsupported
runtime/compatibility evidence rejects safely. Latest-runtime smoke tests do
not certify the older runtime or its Papyrus/save bridge, and vice versa.
No power-loss or canonical two-client mutation claim follows from these
gameplay smoke tests alone.

## Full upstream commit inventory

Chronological topological history from the shared base; includes eight merge
commits. Final source deltas, not intermediate temporary probes, are the unit
of implementation review.

| Commit | Author date | Subject |
|---|---|---|
| [`4fb53dde`](https://github.com/tiltedphoques/TiltedEvolution/commit/4fb53dde7c64771b0e20d564248865998ddaa6e8) | 2026-07-03 | chore: add leveled NPC resolution probe (temporary) |
| [`4e7e6082`](https://github.com/tiltedphoques/TiltedEvolution/commit/4e7e608281100bcfe4a69d2673e0c1d063098ab8) | 2026-07-03 | feat: add LeveledNpcPickId to character assignment and spawn messages |
| [`e37ce17a`](https://github.com/tiltedphoques/TiltedEvolution/commit/e37ce17a66e04f927a4fd4d8fff06fd076baa5bf) | 2026-07-03 | feat: store and relay leveled NPC pick server-side |
| [`dd20290b`](https://github.com/tiltedphoques/TiltedEvolution/commit/dd20290b218389ed22ed607de927ee2497286641) | 2026-07-03 | feat: capture resolved leveled NPC pick on assignment |
| [`227c0053`](https://github.com/tiltedphoques/TiltedEvolution/commit/227c0053b1f432f4091c7eb3187a4e927770aa9f) | 2026-07-03 | feat: conform remote clients to owner's leveled NPC pick |
| [`b3f3ddd1`](https://github.com/tiltedphoques/TiltedEvolution/commit/b3f3ddd19efe1de4d4c7af0a43f2a7bced138230) | 2026-07-03 | fix: harden leveled pick capture against races and forced-pick bleed |
| [`970a772c`](https://github.com/tiltedphoques/TiltedEvolution/commit/970a772cd05dd3eada25414c600c48c78fc9bf44) | 2026-07-03 | style: drop const prefix from mutable npc id lookups |
| [`47ce3e15`](https://github.com/tiltedphoques/TiltedEvolution/commit/47ce3e1571fed96b5e6bd57ddb726a0a2ab3058f) | 2026-07-03 | fix: relay leveled pick on assignment response and add sync diagnostics |
| [`daab6806`](https://github.com/tiltedphoques/TiltedEvolution/commit/daab68067f8dfe2c3ae3141ee7786f2b560d83bc) | 2026-07-03 | fix: recover leveled pick from template chain when resolver hook missed it |
| [`ca8d8948`](https://github.com/tiltedphoques/TiltedEvolution/commit/ca8d89488137373bc829cd8692cd5f7af25b5f0d) | 2026-07-03 | fix: conform leveled actors by base swap with frame-deferred re-enable |
| [`cf2c6a9d`](https://github.com/tiltedphoques/TiltedEvolution/commit/cf2c6a9d5b077f6b11f872d6385e8382f43e158e) | 2026-07-03 | fix: defer leveled conform until actor 3D settles |
| [`1a6dba9d`](https://github.com/tiltedphoques/TiltedEvolution/commit/1a6dba9d8bcf08fb6d03be9c7dc6bcd450e05168) | 2026-07-03 | fix: hold leveled conform while the loading screen is open |
| [`41c6b8d0`](https://github.com/tiltedphoques/TiltedEvolution/commit/41c6b8d0eaa208c0a76d70edad388d9f15213854) | 2026-07-04 | fix: apply leveled conforms from the service update tick |
| [`2aa1696c`](https://github.com/tiltedphoques/TiltedEvolution/commit/2aa1696cf425c736fd3d0a9a2a5c9f23bf574ece) | 2026-07-04 | fix: prefer template chain over resolver map when capturing leveled pick |
| [`1779088f`](https://github.com/tiltedphoques/TiltedEvolution/commit/1779088fdfb8112aa79df43989fa8d7feec5ab12) | 2026-07-04 | feat: reconcile leveled pick across ownership transfers |
| [`559f0fbd`](https://github.com/tiltedphoques/TiltedEvolution/commit/559f0fbd49d016203ed6dd8f866b5af278becf00) | 2026-07-04 | fix: recompute graph descriptor when conforming leveled npcs |
| [`f609305e`](https://github.com/tiltedphoques/TiltedEvolution/commit/f609305e7824eb413e738ddd167cd6698d67a52d) | 2026-07-04 | fix: bounds-check animation variable serialization |
| [`fe27d189`](https://github.com/tiltedphoques/TiltedEvolution/commit/fe27d189c94537a712ffe325edca9c4c284c53ce) | 2026-07-04 | chore: ignore local tooling and workspace files |
| [`a8065f43`](https://github.com/tiltedphoques/TiltedEvolution/commit/a8065f43fed5125b04f9596f19ffd8701cf81cc6) | 2026-07-05 | fix: conform shell-based actors and keep pending conforms until 3D exists |
| [`200c5d41`](https://github.com/tiltedphoques/TiltedEvolution/commit/200c5d41b2a7ab28365df74ac73c0e57fde42e0d) | 2026-07-05 | tweak: demote leveled pick storage and relay logging to debug |
| [`6f963497`](https://github.com/tiltedphoques/TiltedEvolution/commit/6f9634979310cf1887a1dbf8f74ef13b71d0387a) | 2026-08-22 | SSE 1.7.99 and SKSE changes (#885) |
| [`a54c4010`](https://github.com/tiltedphoques/TiltedEvolution/commit/a54c4010d522458a73c263391d3986791d6a6071) | 2026-08-22 | Add "Hide password" checkbox to the main connect dialog (#873) |
| [`3832a044`](https://github.com/tiltedphoques/TiltedEvolution/commit/3832a044bb52b2a23622d9cba510f7094b7c84f4) | 2026-08-22 | Prevent party auto-join when bAnnounceServer is true to prevent public server trolling (#874) |
| [`9df6c400`](https://github.com/tiltedphoques/TiltedEvolution/commit/9df6c400c09eeb3421c97e711140de19eed40e74) | 2026-08-22 | SkyrimTogetherQuestPatches.esp: patch Companions C02 "Brotherhood" (induction) so member does not hang (#855) |
| [`b163e457`](https://github.com/tiltedphoques/TiltedEvolution/commit/b163e457c7cba950e70eed3bc6ae82c82e081204) | 2026-08-25 | CI: create GitHub Release, multi-platform build, fix linux workflow (#842) |
| [`6f0faa17`](https://github.com/tiltedphoques/TiltedEvolution/commit/6f0faa176bb5415f3c1a2567a18568d4c3f94e26) | 2026-08-25 | Revert "Slow effect is player-specific, should not sync. (#856)" (#886) |
| [`2e52a704`](https://github.com/tiltedphoques/TiltedEvolution/commit/2e52a704354a953276ff0b707890fb9a640376cb) | 2026-08-26 | Fix a bug where camera sometimes gets stuck after a respawn (by using steady-clock instead of delta-time in the player respawn logic) |
| [`97dc28ad`](https://github.com/tiltedphoques/TiltedEvolution/commit/97dc28ad369943ec906bbd527fedd2cd2406cdb1) | 2026-08-26 | Respawn overrides system for interior cells deemed problematic to die in (#875) |
| [`1f488f6f`](https://github.com/tiltedphoques/TiltedEvolution/commit/1f488f6fef7a856d857e1024f1d8bea26cf94b87) | 2026-08-27 | Replace optimistic client ownership with versioned server grants |
| [`97ce31f1`](https://github.com/tiltedphoques/TiltedEvolution/commit/97ce31f1b04b3bd0649140969b72a842f2c99642) | 2026-08-28 | Remove stale `CancelAssignmentRequest` unit test |
| [`d31ade63`](https://github.com/tiltedphoques/TiltedEvolution/commit/d31ade633bf78314be3364d6c5d0caa2af93453a) | 2026-08-28 | Clean up spdlogs, add some useful comments, `OwnershipTransferReason` enum |
| [`ef5c9145`](https://github.com/tiltedphoques/TiltedEvolution/commit/ef5c9145f534330a2975593b1897ed0057bca6ef) | 2026-08-28 | Centralize the ECS lookup as `FindEntityByServerId` in `Utils` |
| [`20d5e0fa`](https://github.com/tiltedphoques/TiltedEvolution/commit/20d5e0fae4ff4e03a3b9cd0a0d4f3eab81e1cf3e) | 2026-08-28 | Remove `IsRemoteActorInteraction` |
| [`adecbfcc`](https://github.com/tiltedphoques/TiltedEvolution/commit/adecbfcc06d382c2415e98fb39dede102edd4e15) | 2026-08-28 | Send `NotifyOwnershipTransfer` directly to the old owner, regardless of whether they remain in range |
| [`42a518a0`](https://github.com/tiltedphoques/TiltedEvolution/commit/42a518a0581a90ef74e454900a22888af605241d) | 2026-08-28 | Fix inventory changes not broadcasting when pickpocketing |
| [`79df6a6d`](https://github.com/tiltedphoques/TiltedEvolution/commit/79df6a6d2f15079e308ffea387cff655258bbde9) | 2026-08-29 | Merge pull request #887 from miredirex/ownership-epochs |
| [`d2bcf04f`](https://github.com/tiltedphoques/TiltedEvolution/commit/d2bcf04fd262b32e4a2693a2822a28fb89b03b9c) | 2026-09-05 | Nix: bump the flake and reduce it to a linux server dev shell (#888) |
| [`92980e22`](https://github.com/tiltedphoques/TiltedEvolution/commit/92980e2214af99b03996f53e98ebb67e04ac0b1c) | 2026-09-06 | Fix dragons occasionally failing to spawn for party members |
| [`24dc4c4e`](https://github.com/tiltedphoques/TiltedEvolution/commit/24dc4c4e8c4744b36ff86f1f129a1c8e3d716356) | 2026-09-06 | Show "incompatible game version" error popup if mismatch is detected |
| [`1660eb0c`](https://github.com/tiltedphoques/TiltedEvolution/commit/1660eb0cb3d48dc885d8d0cf649158df79a61e7c) | 2026-09-10 | Prevent ownership blacklists from persisting and reject former-owner state updates |
| [`05be672e`](https://github.com/tiltedphoques/TiltedEvolution/commit/05be672ea98b00663d2fcc34c47ae7eac400223a) | 2026-09-11 | Merge branch 'dev' into leveled-npc-claude-baseline |
| [`12937348`](https://github.com/tiltedphoques/TiltedEvolution/commit/12937348594ce13bec655a3d0876bfc6ccfadaf4) | 2026-09-11 | Port RE stuff from the other branch, update structs |
| [`97c11cdd`](https://github.com/tiltedphoques/TiltedEvolution/commit/97c11cdd4d7af876e95e0d18a6c9953bc5e3d6da) | 2026-09-11 | Create `LeveledNpcSystem`, move `IsUnresolvedLeveledShell` there |
| [`8770b370`](https://github.com/tiltedphoques/TiltedEvolution/commit/8770b370c72abec88f135c7e4ce248265b8f20b4) | 2026-09-11 | Fix fn declaration of ID 14375; it corresponds to CreateTemplateActorBase, not SetLeveledNpc |
| [`158fd80f`](https://github.com/tiltedphoques/TiltedEvolution/commit/158fd80f0ed7eaf1324473c0c8e21a8d75fec378) | 2026-09-11 | Extend the RE of `TESActorBase` and `TESActorBaseData` a bit |
| [`ac0c25b6`](https://github.com/tiltedphoques/TiltedEvolution/commit/ac0c25b6f602fae0841d8c21153ddd319df0f39e) | 2026-09-11 | Move hook from `TESNPC` to `TESActorBaseData` |
| [`17887601`](https://github.com/tiltedphoques/TiltedEvolution/commit/178876019cab8f7405733c6b7eef02881e11b781) | 2026-09-12 | Experiment with hooking `HookSetLeveledCreature` |
| [`6e8f4eed`](https://github.com/tiltedphoques/TiltedEvolution/commit/6e8f4eed6e11d3ccbfdd7dc5db06c81745e4568f) | 2026-09-12 | Replace leveled NPC pick cache with actor extra data `Actor::GetLeveledPick()` |
| [`ee21fa7c`](https://github.com/tiltedphoques/TiltedEvolution/commit/ee21fa7cc6ab7f174f69c2a1c99b4ce0bd361954) | 2026-09-12 | Introduce `ReconciliationStage` to `ActorExtension` to keep track of lifecycle |
| [`5cc4a774`](https://github.com/tiltedphoques/TiltedEvolution/commit/5cc4a774877e12dd9683afa80f59d4506fc933fc) | 2026-09-12 | Fix TESObjectCELL layout and parent-cell accessors |
| [`951fe612`](https://github.com/tiltedphoques/TiltedEvolution/commit/951fe6124a5395ff283d41f6c5ef189199013f83) | 2026-09-13 | Keep some useful logging related to leveled NPC reconciliation |
| [`36afeebb`](https://github.com/tiltedphoques/TiltedEvolution/commit/36afeebb7b5b88f19283d52e4dab9f1cbd65ac95) | 2026-09-13 | Don't leave the actor disabled if we disconnect before re-enabling it |
| [`8a3cec96`](https://github.com/tiltedphoques/TiltedEvolution/commit/8a3cec96c4955193df9a0f5e33e75b5364e63dd1) | 2026-09-13 | Fix dialogue sync when the conversation participant doesn't own the NPC (#896) |
| [`f765aa6d`](https://github.com/tiltedphoques/TiltedEvolution/commit/f765aa6d718f824acfbb8975a0194e0f1b4abdc8) | 2026-09-14 | Separate client logs for concurrent instances |
| [`98055bf7`](https://github.com/tiltedphoques/TiltedEvolution/commit/98055bf7fff947f3e66fbebd2e6eaa0807088c3f) | 2026-09-14 | Tweak leveled NPC sync so first canonical pick wins |
| [`932ae7ad`](https://github.com/tiltedphoques/TiltedEvolution/commit/932ae7ada7c984db5f2acf9348738d86e031a1d3) | 2026-09-14 | Wait for leveled actor to finish disabling before applying pick |
| [`99790403`](https://github.com/tiltedphoques/TiltedEvolution/commit/997904033047243de99a09936c6ea69d0f38e2ad) | 2026-09-14 | Skip re-enabling actors in OnCharacterSpawn while reconciliation is WaitingForDisable |
| [`865449a4`](https://github.com/tiltedphoques/TiltedEvolution/commit/865449a48080095f9ba90e0ef2a0a58aed5357eb) | 2026-09-14 | Fall back to `BaseId` if `LeveledNpcPickId` produces a null TESNPC |
| [`abefcb08`](https://github.com/tiltedphoques/TiltedEvolution/commit/abefcb08583aee8a19b99215633f4d18d6eadafd) | 2026-09-14 | Merge pull request #892 from miredirex/leveled-npc-sync-attempt2 |
| [`5aec3487`](https://github.com/tiltedphoques/TiltedEvolution/commit/5aec34878938994edcb8d292b3eea8661e680040) | 2026-09-14 | Skip spell cast sync for summon effects from staves (#894) |
| [`25bedc9d`](https://github.com/tiltedphoques/TiltedEvolution/commit/25bedc9d23469f787b097599696e31b54f969f47) | 2026-09-14 | Fix possible null pointer deref on AnimationComponent |
| [`e0002073`](https://github.com/tiltedphoques/TiltedEvolution/commit/e0002073212a96334a7bd8e221ce622606adcdf4) | 2026-09-14 | Fix "Clear stuck screen effects" mutating the list it is iterating |
| [`eacc3699`](https://github.com/tiltedphoques/TiltedEvolution/commit/eacc36999aa6e91071c15cbfd37bca9351307d17) | 2026-09-14 | Merge pull request #889 from tiltedphoques/dev |
| [`10e64706`](https://github.com/tiltedphoques/TiltedEvolution/commit/10e64706490bbb0b425d5f39728c0510ca1b9c50) | 2026-09-19 | Correctly rebuild leveled NPC base from the original NPC and the owner's pick (#899) |
| [`59669245`](https://github.com/tiltedphoques/TiltedEvolution/commit/59669245742eec942fc1d480d6d45c3d617f895b) | 2026-09-19 | Exclude faction jail containers from object sync (#893) |
| [`0ffb80b0`](https://github.com/tiltedphoques/TiltedEvolution/commit/0ffb80b015ac31d5df935b18311aad2c42fc5ce8) | 2026-09-20 | Merge pull request #900 from tiltedphoques/dev |
| [`589b5f5b`](https://github.com/tiltedphoques/TiltedEvolution/commit/589b5f5b16bbfe61f5a1840068f9a5207e9a8b16) | 2026-09-20 | Fix double zip in CI for non-release builds |
| [`fbf72883`](https://github.com/tiltedphoques/TiltedEvolution/commit/fbf72883015dba9e26bd1539d3af9d33fa794116) | 2026-09-22 | Fix havok corruption on remote actors by keeping controller timestep valid (#901) |
| [`3e68a605`](https://github.com/tiltedphoques/TiltedEvolution/commit/3e68a605824d786e989aa432f1f2852555bb748a) | 2026-09-28 | Add DLC1DimHollowCrypt02 to CellRespawnOverrides.cpp (#902) |
| [`3941ddd0`](https://github.com/tiltedphoques/TiltedEvolution/commit/3941ddd0963756a476226a45558586814dbcd538) | 2026-09-30 | Remove unused AnimationExperiments.cpp (#905) |
| [`23e47d2c`](https://github.com/tiltedphoques/TiltedEvolution/commit/23e47d2c24e2a830041ffe86f21a1033d9e149ae) | 2026-10-02 | Add `--generate-config` command-line arg to generate STServer.ini |
| [`6847eaa0`](https://github.com/tiltedphoques/TiltedEvolution/commit/6847eaa0423332b5d282be2e3f49c2a6bedfbd0d) | 2026-10-02 | Update some of the `Console::Setting` descriptions |
| [`8f08d405`](https://github.com/tiltedphoques/TiltedEvolution/commit/8f08d405ff3072cfd02a301983357d6b5950e8c8) | 2026-10-03 | Fix rapid NPC duplication caused by owners applying stale assignment state (#906) |
| [`a89d8185`](https://github.com/tiltedphoques/TiltedEvolution/commit/a89d81859311041fac1d326fcbd25c2a703b0da8) | 2026-10-03 | Show "your installation is not purely vanilla" only in master builds |
| [`af7f37e6`](https://github.com/tiltedphoques/TiltedEvolution/commit/af7f37e6a453a0b965a491027721d2361a21a4d9) | 2026-10-03 | Fix uninitialized DesiredTarget in SpellCastRequest (#907) |
| [`a3f62ba7`](https://github.com/tiltedphoques/TiltedEvolution/commit/a3f62ba7c45327afb593598d00eeafd9599243d2) | 2026-10-03 | Suggest forming a party when questing solo with others on the server (#909) |
| [`92d28f07`](https://github.com/tiltedphoques/TiltedEvolution/commit/92d28f070e9d1ed0ae58036a909fe59aba17a71d) | 2026-10-03 | Add Swedish localization (#910) |
| [`4917189f`](https://github.com/tiltedphoques/TiltedEvolution/commit/4917189f6ac382e874f9ee735c4fef6a350e449e) | 2026-10-03 | Merge pull request #908 from tiltedphoques/dev |
| [`accff5c8`](https://github.com/tiltedphoques/TiltedEvolution/commit/accff5c843ce713c61e389a00b02d22170bd29cb) | 2026-10-05 | Update fr.json and pl.json |
| [`d2eaf9bf`](https://github.com/tiltedphoques/TiltedEvolution/commit/d2eaf9bf055ce6902e693227adbbbb227bd2a939) | 2026-10-05 | Reduce server log spam and fix mismatched spdlog args in `QuestService` |
| [`9ce29715`](https://github.com/tiltedphoques/TiltedEvolution/commit/9ce297154252db37c732f12291569944eb94c7c6) | 2026-10-06 | Bump TiltedUI to pull in the `do-not-de-elevate` change |
| [`115b5019`](https://github.com/tiltedphoques/TiltedEvolution/commit/115b5019609b96eb9d63bbd03962043c30437dc8) | 2026-10-06 | Fix rare Havok crash when an actor re-rolls into a different creature (#917) |
| [`a813c61e`](https://github.com/tiltedphoques/TiltedEvolution/commit/a813c61e01ca9cbb68734d7217cc15741812bcc3) | 2026-10-05 | Fix KiLoader plugins failing to find their DLL dependencies |
| [`23901108`](https://github.com/tiltedphoques/TiltedEvolution/commit/23901108931fe3bc20c9e1f75ef987a702e2f08b) | 2026-10-10 | Replace fixed 300 ms remote delay with adaptive playback |
| [`5a4a8eae`](https://github.com/tiltedphoques/TiltedEvolution/commit/5a4a8eaec52b9d6cd5b3123471131014136a2b64) | 2026-10-10 | Fix crash when spawning a remote actor that is still loading |
| [`0a6784b8`](https://github.com/tiltedphoques/TiltedEvolution/commit/0a6784b855ebf87c424217124b29335229454308) | 2026-10-10 | Reset remote playback on ownership change and relay sooner |
| [`b0e46d61`](https://github.com/tiltedphoques/TiltedEvolution/commit/b0e46d613705efbff461d017302ab44f9ede4c6a) | 2026-10-10 | Bump TiltedConnect to include the Nagle change |
| [`a4a33744`](https://github.com/tiltedphoques/TiltedEvolution/commit/a4a337449944ddf6b6601a6f7bfa68a44871b342) | 2026-10-11 | Stop remote NPCs stuttering for seconds after leaving combat |
| [`fb847a8c`](https://github.com/tiltedphoques/TiltedEvolution/commit/fb847a8c20740741d5c5d0ec7139229e1a19301b) | 2026-10-11 | Merge pull request #921 from miredirex/feat/adaptive-interpolation |
| [`34cb0254`](https://github.com/tiltedphoques/TiltedEvolution/commit/34cb0254fcbc31ee4d3747e2e61a74af543df638) | 2026-10-12 | Allow repositioning network debug stats to any screen corner (#924) |
| [`11add82a`](https://github.com/tiltedphoques/TiltedEvolution/commit/11add82a72cd3209907817f8bbe972fe5d01debf) | 2026-10-12 | Add Ukrainian translation (#923) |
| [`e65a4069`](https://github.com/tiltedphoques/TiltedEvolution/commit/e65a406957fc59b5fd0de079e43862767612a65d) | 2026-10-12 | Nuke the unused admin protocol and other dead xmake targets |
| [`9ce03385`](https://github.com/tiltedphoques/TiltedEvolution/commit/9ce0338522277e0ff0a12e8d2aa09ce67ad41a59) | 2026-10-12 | Drop leftover dead files after the admin protocol removal |
| [`cae24433`](https://github.com/tiltedphoques/TiltedEvolution/commit/cae244337127a9dd034a8dcbbd58dc87b92c7e5a) | 2026-10-14 | Debug helpers: restore player controls (#916) |
| [`390071ca`](https://github.com/tiltedphoques/TiltedEvolution/commit/390071cad4b828ea6c56a561f248ebb4cb4368c3) | 2026-10-14 | Fix BSAnimationGraphManager reference leaks when syncing animation vars (#920) |
| [`0882e77d`](https://github.com/tiltedphoques/TiltedEvolution/commit/0882e77da6700c9107a9a8d915cf936c5d664c46) | 2026-10-15 | Add AGENTS.md, add `addrlib.py`, update README.md (#925) |
| [`b0f0f559`](https://github.com/tiltedphoques/TiltedEvolution/commit/b0f0f559fd569d0f2bb255619ac1a95e1336622a) | 2026-10-15 | Update AGENTS.md: add "for changes that depend on engine behavior..." |
| [`ea0f6c39`](https://github.com/tiltedphoques/TiltedEvolution/commit/ea0f6c390f614706e1d8e76230a413c8c07e1445) | 2026-10-16 | Fix cutscene voice lines and subtitles desyncing in a party (#854) |
| [`69fe8e67`](https://github.com/tiltedphoques/TiltedEvolution/commit/69fe8e6704789ad0e9c88cd13cd80ef34df20129) | 2026-10-16 | Stop scripted slow time sticking to other party members |
| [`e9214a87`](https://github.com/tiltedphoques/TiltedEvolution/commit/e9214a876cdae41e73da6a8a5ec45d4948cf21f6) | 2026-10-18 | Rework party quest progression to stop duplicate stages and stuck cutscenes (#848) |
| [`5a72cdcd`](https://github.com/tiltedphoques/TiltedEvolution/commit/5a72cdcd17b998fb1462bb5c6fb0d60d29b98de9) | 2026-10-19 | Fix silent cutscene lines for party members stuck in a scene phase (#928) |
| [`e1de917c`](https://github.com/tiltedphoques/TiltedEvolution/commit/e1de917ce206d8df35982584e11272a6d569c1bc) | 2026-10-21 | Add a skyrim_version build option for 1.6.1170 support; update CI |
| [`43bfd173`](https://github.com/tiltedphoques/TiltedEvolution/commit/43bfd173b65f6541f50df15ff634245ce64da398) | 2026-10-22 | Fix combat crash near players with shouts from mods you don't have |
| [`54b3a858`](https://github.com/tiltedphoques/TiltedEvolution/commit/54b3a858e228dc8807ed65bc3f9ac3c3f81b610d) | 2026-10-22 | Merge pull request #930 from tiltedphoques/dev |
| [`cc4a108a`](https://github.com/tiltedphoques/TiltedEvolution/commit/cc4a108ad41828663df0d8a382ad38f03a17efa1) | 2026-10-24 | Fix a crash when a party member's waypoint has no worldspace (#934) |
| [`5a870f03`](https://github.com/tiltedphoques/TiltedEvolution/commit/5a870f03905cb12e6ed8393b2db4543bbac6cf90) | 2026-10-27 | Fix README badges |
| [`25a0049a`](https://github.com/tiltedphoques/TiltedEvolution/commit/25a0049a00900b8714e105f795380caf33678012) | 2026-10-27 | Fix freezes caused by a leaked `ExtraDataList` lock (#937) |

## Outcome of this preparation

Executed preparation checks: documentation validation passed (41 Markdown
files and 15 ordered task specifications); git diff --check passed; every
recorded one of the 107 commit identities and 354 changed paths matched the
pinned Git objects. The main checkout stayed clean and the pre-existing
authorization-worktree modifications stayed intact. These are preparation
checks, not execution of the merged TPTests or product builds.

The upstream brings material fixes relevant to prior NPC, inventory, death,
dialogue and freeze reports. It is feasible to prepare one reviewed integration
candidate, but a conflict-free or mechanically safe merge has been disproved.
The prepared evidence identifies exact conflicts, cross-file dependencies,
deliberate resolution rules and test/live gates.

Implementation, merged CI and runtime acceptance remain outstanding.
Canonical mutation remains disabled; **P0 NOT CLOSED**.
