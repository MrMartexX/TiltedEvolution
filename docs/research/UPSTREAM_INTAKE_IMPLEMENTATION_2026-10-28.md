# Upstream intake implementation — 2026-10-28

Status: Candidate published; required hosted CI passed on 80894cbc; live acceptance incomplete. P0 NOT CLOSED.

This candidate follows the [approved conflict resolution review](UPSTREAM_CONFLICT_RESOLUTIONS_2026-10-28.md) and its [36-region manifest](UPSTREAM_CONFLICT_RESOLUTIONS_2026-10-28.json). Historical research remains unchanged.

## Inputs and isolation

- Refreshed integration source: d6a8ab205d3223facd30f54cc0f52d50bd1fe542.
- The integration advanced one commit after the review's 99b4d080 baseline.
- That commit introduced a controlled native SetStage route and a research guide; the approved intake requires canonical mutation to remain disabled.
- Reviewed upstream: 25a0049a00900b8714e105f795380caf33678012.
- Working branch: codex/upstream-dev-candidate.
- Research documentation parent: 4e65fd3b (code identical to d6a8ab20).
- Working directory: D:/Codex/Workspaces/2026-10-28/upstream-dev-candidate.
- Baseline validation directory: D:/Codex/Workspaces/2026-10-28/upstream-dev-review.

The refreshed diagnostic merge still had the same 20 conflicting files / 36 regions. Existing integration/dev branches and the pre-existing authorization checkout were preserved.

## Implemented resolutions

| Group | Implementation | Remaining acceptance |
|---|---|---|
| R1 | Shared production InventoryChangePolicy validates authority before one checked delta and derives relay/drop decisions. Server uses it. Respawn keeps checked gold loss and adds current ownership epoch. | Real respawn/pickpocket/loot with multiple clients. |
| R2 | Stable fork opcode values; retired client 1 and server 16/44 slots reserved/rejected. Action messages appended at 55/59. Factories retain PartyQuest, remove retired registrations and reject missing/truncated dispatch entries. | Hosted Linux/Windows build/tests; mixed-build connection test. |
| R3 | Existing legacy service, canonical dry-run routing, lifecycle subscriptions, exact sink teardown and server coordinator retained. Held-stage queue and associated server dedup not activated. | Later bounded held-stage port requires the documented owner/completion contract. |
| R4 | Expanded scene declarations retained for dialogue; typed aliases and old stage representation/TESQuest methods retained. | Native scene ABI and intended content/compatibility evidence. |
| R5 | Consistent runtime-conditioned GameVM type in the VM hook, with fork MainMenu/thread fences intact. Version rejection occurs before Address Library/hooks. Separate runtime output directories now include generated headers correctly. | Exact-runtime native/layout and gameplay validation for both admitted binaries. |
| R6 | Fork bounded Address Library parser/publication and native hook checks retained. Coordinated LoadScriptExtender rename; early call and checked DLL discovery retained. Startup logs describe hook installation. | Native startup/bridge evidence on candidate artifacts. |
| R7 | Zero-section plaintext bypass excluded. Launcher uses a tested header/flag decision with signature/OEP rejection. Existing SteamAppId offset fix retained. | No new alternate SteamStub variant accepted; full mapped-image live proof pending. |
| R8 | Pinned root dependencies retained; redundant client tiltedcore/recast removed. Portable PlaybackClock plus Windows bridge test sources retained. Real portable replay-cache source added for regression coverage. | Both native product configurations and hosted platform gates. |
| R9 | Full builds/tests execute without scope skipping; matching Linux 22.04 protobuf baseline retained; xmake 3.1.0 aligned in workflows. Windows runtime-specific concurrency/artifacts/output paths, one packaging owner, bridge staging and permanent test logs retained. | Hosted execution remains blocked externally; release/arm64 admission deferred. |
| R10 | Bounded wire-compatible action decoder validates nested counts, strings, masks, widths, truncation and duplicate actor IDs before publication. Invalid messages are rejected by both handlers. Relay and replay share the filtered accepted set. | Animation ownership-incarnation limitation remains; native locks/dialogue need runtime validation. |

The new action codec is specific to differential actions serialized against ActionEvent{} in the new messages. It does not silently change the existing general differential codec. The checked seven-bit-plus-continuation encoding was compared with [TiltedCore v0.2.9](https://github.com/tiltedphoques/TiltedCore/blob/v0.2.9/Code/core/src/Serialization.cpp); existing wire serialization is unchanged. Project attribution is retained.

The native executor again contains no canonical TESQuest::SetStage call. A controlled research/mock activation flag cannot bypass the production durability policy. The pure mock adapter/tests from the refreshed integration remain available but have no connected native engine callback.

## Deliberate exclusions and exact dependency

1. Upstream #848 held-stage execution and associated server stage dedup: missing immutable lifecycle/operation completion proof; legacy baseline retained.
2. SteamStub zero-section success bypass: zero metadata is not plaintext proof.
3. Leveled-NPC asynchronous runtime rebuilding: removed the connected queue, all enqueue/apply calls, phase-specific spawn behavior, disconnect re-enable cleanup and discovery retention as one unit. Server pick metadata/capture remains available; source presence is not runtime rebuilding admission.
4. Linux arm64/toolchain migration and automatic release publication: not admitted. The release workflow explicitly blocks publication; the fork cannot publish the upstream Docker destination.

For (3), a generation check alone cannot prove asynchronous disable completion or safely undo a disable after invalidation. The existing process fence can deny stale execution, but safe cancellation must establish whether the original operation still owns the original reference and finish/undo before it loses that identity. A post-transition FormID lookup cannot provide that proof. Dropping only the pending map could leave a still-current NPC disabled; applying cleanup after LoadGame could enable a reused actor. No speculative cleanup was introduced.

For the action stream, current OwnerView validation remains upstream behavior. The payload lacks an ownership incarnation; the intake does not claim epoch-complete replay protection or expand the wire schema to invent it.

## Checks and evidence

Local tools: MSVC 2022 14.44, xmake 3.1.1+HEAD.3ba37a0d4. Workflows pin 3.1.0 and still need their own execution. All new work/build/cache/temp paths used drive D.

Exact refreshed baseline d6a8ab20: Windows release TPTests passed, **287023 assertions / 1022 test cases**. A separate earlier documentation-parent build produced the same counts; it is not substituted for the exact baseline rerun.

Initial candidate focused suite passed **289 assertions / 14 test cases**. Full candidate TPTests then passed **287344 assertions / 1035 cases** before additional cumulative-budget regressions; these are historical intermediate results.

After the cumulative-budget regressions, focused intake/playback checks passed
**332 assertions / 17 cases**, and the full Windows 1.6.1170 configuration passed
**287356 assertions / 1037 cases**. The checked action map uses the project's
SortedMap so its publication swap is compile-time verified noexcept; changing
actor traversal order does not change the map's wire schema or per-actor action order.

Final tested code SHA: **f806ee0a6ff32e8e059402bf84e2116672550e1b**.
The following results are bound to that code commit. A later documentation-only
commit records them; it does not receive implicit CI or gameplay acceptance.

| Windows x64 release configuration | Product builds | Focused TPTests | Full TPTests |
|---|---|---|---|
| 1.6.1170 | All six targets passed | 332 assertions / 17 cases | 287356 assertions / 1037 cases |
| latest (1.7.104) | All six targets passed | 332 assertions / 17 cases | 287356 assertions / 1037 cases |

The six targets were SkyrimTogetherClient, SkyrimImmersiveLauncher,
SkyrimServerRunner (including STServer), PartyQuestCompatibilityAnalyzer,
PartyQuestSkseLineageBridge and TPTests. Both server config-generation smoke
checks returned zero. This did not start a multiplayer server or Skyrim session.

The [machine-readable validation record](upstream-intake-2026-10-28/validation.json)
contains artifact SHA256 values, x64 PE checks and the matching build identity in
both SkyrimTogether.exe and STServer.dll for both configurations.

Reusing a pre-commit xmake configuration initially retained an old git-describe
stamp. That artifact was not admitted: clean configuration and rebuilding
replaced it, and both binary identities were verified against the tested commit.
After changing HEAD, use clean xmake configuration and check the generated
identity before pairing incremental client/server artifacts.

Additional component suites: Console_Tests **6 passed**, Resources_Tests
**13 passed**. ESLoader_Tests initially lacked Data fixtures; with the five real
base ESMs and loadorder.txt, **1 passed / 7 failed**. ESLoader::BuildRecordCollection
returns an empty collection while its record-loading code is commented out.
The loader and its tests are byte-identical to the refreshed baseline. This is
a recorded pre-existing unfinished component, not an intake regression or a
green optional suite. Its implementation was not broadened or its tests weakened.

Production UI also built successfully with pnpm 9.15.9 and local Node 24.19.0;
hosted CI's pinned Node 18 configuration remains separate evidence to obtain.

Offline analyzer comparisons were repeated with the final code's analyzer and
all **19 base BSA archives / 14013156171 bytes** mirrored by hard links, plus any
base loose PEX files. The [base result](upstream-intake-2026-10-28/base-environment.json)
exactly reproduces the existing reference environment: plugins 0x62557d41e3e9a150,
scripts 0x28ae1c01a4a2ead7. With both shipped STR plugins, the
[intake result](upstream-intake-2026-10-28/with-str-plugins.json) has plugins
0xbbd5af5215bc4c47 and the same script fingerprint. Both reviewed quest topologies
and winning plugin identities remain unchanged. Their winning-override
fingerprints change because that fingerprint includes the plugin environment.

These are read-only offline evidence and do not represent the complete MO2 VFS
or grant runtime/mutation authority. The initial reduced-archive experiment's
different script digest was not used. Reviewed profile constants were not edited
to admit the new environment; full intended runtime-content admission remains
a gate before integration/activation.

Existing Windows symlink tests warn that directory-symlink creation privilege is unavailable locally. Those specific branches have not acquired new coverage merely because the overall suite passed.

Actionlint v1.7.12 was downloaded from its official release on drive D, checked against its published SHA256 checksum list, and passed the candidate workflows with shellcheck/pyflakes unavailable. This proves workflow syntax/expression checks, not hosted build execution or shell-script runtime success.

GitHub dispatch of Build Linux, Build Windows and diagnostics returned HTTP 422: Actions has been disabled for this repository. Repository settings simultaneously reported enabled=true and active workflows. The user's GitHub banner confirmed the fork-specific suspension due to the scale of Actions usage. This is an external execution blocker, not a failed C++ test or a green CI signal.

No candidate CI acceptance, live Skyrim run, power-interruption test or merge into the integration/dev branches is claimed. The tested code commit itself is an isolated upstream merge candidate.

## Follow-up evidence — 2026-10-29

Fork Actions were re-enabled. All following runs completed successfully on exact
80894cbc68f192277d03f67d504e13d37ba9181d; the earlier suspension above is historical.

| Hosted validation | Assertions | Cases |
|---|---:|---:|
| [Linux full build](https://github.com/MrMartexX/TiltedEvolution/actions/runs/40764786774) | 286380 | 983 |
| [Windows full build, 1.6.1170](https://github.com/MrMartexX/TiltedEvolution/actions/runs/40764792137) | 287367 | 1037 |
| Windows full build, latest/1.7.104, same run | 287367 | 1037 |
| [Linux diagnostics](https://github.com/MrMartexX/TiltedEvolution/actions/runs/40764797515) | 286380 | 983 |
| Windows diagnostics, same run | 287367 | 1037 |

Actual required build/test steps executed; optional release packaging was skipped.
Runtime-separated live Windows artifacts exist. Full logs were retained locally at
D:/Codex/Artifacts/upstream-intake-2026-10-28/hosted-80894cbc.
These results bind to 80894cbc, not to a later documentation-only commit.

The same HEAD was rebuilt locally for 1.6.1170 and installed as a separate MO2 mod.
Client/server build identities match. An initial high-parallelism build exhausted
Windows commit; rebuilding with two jobs succeeded. During the first live save
load Windows event 2004 recorded commit charge 98136285184 of limit 98149163008;
the game and other system processes crashed. The dump shows a write access
violation in a runtime memory-copy routine called from mapped game code. This
supports memory exhaustion as a lead, not a proven exclusive root cause or save
corruption diagnosis.

The user subsequently reports no crash and no immediate clothing regeneration
after theft. Follower trade still opens inventory with dialogue overlaid. The user
reports this symptom also existed before intake; a controlled baseline comparison
has not established equivalence. The current log records a LoadGame transition
at 01:10:13, connection at 01:11:46 and missing server identity for dialogue actor
0x1A6D6 at 01:17:55 (local log times). That error path exists in the baseline and
returns before sending a dialogue request. It does not establish the UI bug's cause.

Do not silently turn partial gameplay evidence into completed T1–T10 acceptance.
Remaining evidence includes dialogue/trade regression classification, save/load/menu
transitions, courier/guard behavior, two-client ownership handoff/reconnect and
the exact native/content checks required by the approved plan. No canonical mutation
or save repair is authorized by these tests. Integration/dev still remain unchanged.

## Connected follower trade crash — 2026-10-29

User clarification: the item-transfer crash was observed only once on the fork;
no repeat attempt established reproducibility, and no crash was observed during
the official-mod comparison. The dialogue/inventory overlay is a distinct symptom
reported in both clients. Do not describe the crash itself as reproduced upstream.

The user isolated the overlay symptom to the connected state: disconnected trade
works, whereas connected trade leaves dialogue above inventory. Transferring items
in that connected state also crashed the exact deployed 80894cbc build. Treat the
overlay and crash as two observations; a common root cause is not yet established.

At local log time 01:48:46.027–.029 the client reported item 0x13ED9 count -6 for
player 0x14 and +6 for actor 0x1348C, followed by equipment events. At .030 thread
2192 raised c0000005 at mapped-game RVA 0x1792B8 while reading 0x10286F.
Register r9 was 0x10285C, r8 was 0x13. The instruction tests a presence-bit byte
using data loaded from object offset 0x10; the caller supplies extra-data type
0x9B (Enchantment in this source). This is consistent with an invalid extra-data
pointer; it does not identify its writer, prove use-after-free, or implicate the
new lock implementation specifically.

WinDbg's initial launcher-module stack could not unwind the mapped game. Loading
the exact local SkyrimSE image's unwind metadata recovered an engine call chain:
0x1792B8 -> 0x16DD2E -> 0x223115 -> 0x92B190 -> 0x8FE7D0 -> 0x8FCC4F ->
0x8FA4C3 -> 0xFA4520 -> 0x6D2399. No fork C++ caller appears in that recovered
chain, which does not rule out earlier hook corruption. Referenced heap contents
are absent from this minidump. There is no new event 2004 near this crash; the
earlier system-wide exhaustion is not an established explanation for this one.

Local evidence: D:/Codex/Artifacts/upstream-intake-2026-10-28/follower-trade-2026-10-29
and crash-follower-trade-*-80894cbc.txt in its parent. The dump remains at
D:/Games/Skyrim Special Edition 1.6.1170/crash_UTC_2026-10-28_23-48-46.dmp;
it was not duplicated. No speculative native fix, broader dump collection,
test weakening or integration update was performed. The next acceptance work is
to classify the connected-trade failure against the exact pre-intake baseline,
then investigate its ownership/lifetime or menu transition cause with sufficient
native evidence. A historical report of similar dialogue symptoms is not proof
that this crash is pre-existing. T1/T10 connected trade acceptance remains open.

Separately, the server started from the MO2 junction-backed mod path rejects
canonical protocol admission because stable-storage node validation fails. That
fail-closed environment issue does not establish the trade-crash cause; future
canonical diagnostics must use the prepared physical D: server directory instead.

## Official-mod comparison: isolation correction — 2026-10-29

The user reports the dialogue/inventory overlap also with official STR 1.8.4's
1.6.1170 release: connecting enables the symptom and disconnecting clears it.
However, this first comparison was not fully isolated from research components.
Process module inspection and skse64.log confirmed the official client loaded
SkyrimTogetherLineageBridge.dll from the physical game Data/SKSE/Plugins directory.
The physical skse64_1_6_1170.dll also contained our PartyQuestSKSE isolated-save
exports (SHA256 341573A64F62640430C43C024DAE3CE9317D0B7266DD97CC07D8A0601233D070).
Disabling fork mods in MO2 did not hide these physical files. The earlier claim
that the official comparison contained no fork components was therefore incorrect.
Neither contamination nor shared symptoms prove the root cause or baseline equivalence.

After the user closed Skyrim, both research binaries were preserved under
D:/Codex/Artifacts/official-str-1.8.4/research-components-backup. The physical
bridge was removed from discovery, and SKSE 2.2.6 was restored from its official
archived build: SHA256 C9A2C8A80DF6BF2372C5F49468BB2E5AB67786157265B6F29ECE9F4EAC075D54.
All 62 enabled SKSE PEX files match that archive. Physical plugin discovery now
contains only versionlib-1-6-1170-0.bin. Recheck loaded modules in a fresh process
before counting the next comparison as stock-only. Restore the saved custom SKSE
before future fork save-provider research requiring its extension; do not combine
that restoration with this stock-only control. No user save was edited.

Relevant primary sources: [upstream issue 787](https://github.com/tiltedphoques/TiltedEvolution/issues/787)
reports connected follower-inventory crashes on STR 1.7.1. One commenter could not
reproduce; a contributor suggests Skyrim Souls RE Updated as a menu-crash workaround.
Its closed status is not a demonstrated code fix for this exact overlay or crash.
[Issue 182](https://github.com/tiltedphoques/TiltedEvolution/issues/182) separately
records barter-menu unpause problems. The official v1.8.4 UI.cpp, unchanged from
our intake baseline, clears menu pause/freeze flags for ContainerMenu only while
connected (unless Skyrim Souls is active). This supplies a plausible connection-
dependent mechanism, not proof of the exact faulty transition or crash writer.
No additional menu mod was installed and no speculative native patch was made.

Fresh stock-only startup was subsequently verified: process 24124 uses the exact
official 1.8.4 client binary, loads stock SKSE 2.2.6 and has no LineageBridge module;
SKSE's fresh log reports no plugin listeners. Gameplay comparison remains pending.
MO2's global configuration stalled on Qt settings-file locking through the C:
junction; a temporary portable configuration under D:/Games/Modding/MO2 launched
the client using the same D: mods/profiles. Settings backups/restoration notes are
in D:/Codex/Artifacts/official-str-1.8.4. No antivirus/security setting was changed.

Further bounded analysis of the existing fork dump maps frame RVA 0x223115 to
Address Library 15995 + 0x45. The reviewed local CommonLibSSE-NG source identifies
15995 as InventoryEntryData::GetValue. Together with the recovered menu-processing
frame 82082 this locates the crash during item-value calculation for the menu,
while reading extra-data type 0x9B. It still does not identify who invalidated the
pointer earlier: the necessary heap contents/history are absent from this dump.
No larger dump collector or speculative native correction was enabled.

## Final review and user-directed UI deferral — 2026-10-29

The user subsequently confirmed the dialogue overlay in the verified fresh
stock-only official STR process, and explicitly deferred investigating this UI
bug so intake work could continue. It remains a known issue, not a fixed item.
The single observed fork crash remains separately recorded; it was not reproduced
or observed on the official control. No causal equivalence is inferred.

The [final source review snapshot](upstream-intake-2026-10-28/final-source-review.json)
binds the review to e4b4ea2c45742a156b5d2ab833545fb36d7088d9. Subsequent edits in
this slice are documentation/evidence only. It enumerates all 20 marked files /
36 regions and 14 clean overlaps, records blobs against both parents, checks
absence of merge markers, retained ancestry, and exact source equality to hosted
green 80894cbc. The fork QuestService.cpp, VersionDb.h, ModSystem pair, PartyQuest
message pair and license are byte-identical to the refreshed integration baseline.
No new source fix was justified by this review. Mechanical checks and the following
source observations do not substitute for native behavior or multiplayer evidence.

| Plan group | Available evidence | Remaining acceptance or exclusion |
|---|---|---|
| T1 inventory | Checked delta/authorization tests and full CI; single-client theft no longer immediately regenerates clothing | Actual multi-client relay/owner behavior; single crash remains unclassified |
| T2 protocol | Factory registration, reserved-slot rejection, action codec and message tests passed | Mixed-build rejection and changed payload behavior in the intended paired deployment |
| T3 deferred stages | Fork quest service retained; upstream held-stage execution and its companion dedup deliberately excluded | Future port needs its own completion/lifecycle proof; no held-stage feature admitted here |
| T4 quest ABI/content | Retained legacy list/typed aliases; both build configurations compile; offline topology comparison unchanged | Exact-runtime scene/layout observations and complete intended VFS/content admission |
| T5 GameVM/lifecycle | Retained fences, both runtime builds green; candidate startup/load seen on 1.6.1170 | Full save/load/MainMenu matrix and supported-runtime native evidence |
| T6 loader/mapping | Critical fork parser/mapping unchanged; startup/SKSE checks retained; current tests green | Distinguish stock control from custom save-provider environment in future live runs |
| T7 SteamStub | Correct field alignment/flag decision retained; synthetic regressions green; candidate entered the game | Synthetic tests do not prove every executable variant or restoration path |
| T8 build/tests | All required client/server/test targets and exact platform counts recorded | Legacy optional ESLoader stub failures remain separately disclosed |
| T9 workflows | Real Linux/Windows build/test steps and UI completed; runtime artifacts exist | Baseline Linux comparison dispatched below; release/arm64 publication remains excluded |
| T10 connected imports | Bounded action decoder and accepted-only replay tests; async NPC rebuilding excluded as one connected unit | Native lock/thread-domain proof, two-client handoff/reconnect and remote NPC transitions |

The 14 clean overlaps were re-read as connected changes: release publication guard;
animation cleanup; actor inventory/dialogue/controller integration; intrusive-ref
include; discovery/cell changes; epoch-bound inventory; faction stash exclusions;
party-disconnect cleanup; transport receive timing; retained inventory interface;
SteamStub policy; generated include path and README. Imported code is not assigned
canonical mutation authority. In particular the action payload still lacks an
ownership incarnation; current-owner validation is not presented as complete replay
protection. Native ownership-token capture/thread assumptions need evidence, not
a speculative second registry/lifecycle model in this merge.

The exact integration source d6a8ab205d3223facd30f54cc0f52d50bd1fe542 was dispatched
for its missing hosted Linux comparison:
[baseline Linux run 40798492885](https://github.com/MrMartexX/TiltedEvolution/actions/runs/40798492885).
It was still configuring dependencies when this review was recorded. Its current
status must be read before reporting acceptance. No baseline/integration ref was changed.

The candidate is ready for an isolated draft review; this does not mark it ready
to merge. Keep the explicit exclusions in any eventual integration description.

## Completed baseline and d603fb39 hosted comparison

The missing exact-baseline execution evidence is now available for
d6a8ab205d3223facd30f54cc0f52d50bd1fe542:

| Baseline job | Result | Assertions / cases |
|---|---|---|
| [Linux full build 40798492885](https://github.com/MrMartexX/TiltedEvolution/actions/runs/40798492885) | success | TPTests compiled, not executed by this old workflow |
| [Linux diagnostics 40800388854](https://github.com/MrMartexX/TiltedEvolution/actions/runs/40800388854) | success | 286047 / 968 |
| Windows diagnostics, same run | success | 287034 / 1022 |

All required hosted checks also completed on candidate head
d603fb3989541cff5df7abc27615c426e0bdbd81:

| Candidate job | Result | Assertions / cases |
|---|---|---|
| [Linux full build 40799230226](https://github.com/MrMartexX/TiltedEvolution/actions/runs/40799230226) | success | 286380 / 983 |
| [Windows full build 40799230174, 1.6.1170](https://github.com/MrMartexX/TiltedEvolution/actions/runs/40799230174) | success | 287367 / 1037 |
| Windows full build, latest/1.7.104, same run | success | 287367 / 1037 |
| [Linux diagnostics 40799278877](https://github.com/MrMartexX/TiltedEvolution/actions/runs/40799278877) | success | 286380 / 983 |
| Windows diagnostics, same run | success | 287367 / 1037 |

The two PR build workflows checked out synthetic merge
0f64860016dce1a4ca4af34f88c0daa11ac7d168. Its complete tree was compared with
d603fb39 and is identical. Manual diagnostics checked out d603fb39 directly.
All required compile/test/UI/staging steps executed; only optional release
packaging was skipped. The [hosted snapshot](upstream-intake-2026-10-28/hosted-validation-d603fb39.json)
retains job IDs, steps, conclusions and exact counts. Full logs are on D: under
the snapshot's logsDirectory. These results precede the following code change.

## T1 notification publication follow-up

Review against the approved T1 criteria found a coverage gap: the original
policy tests asserted stored counts and Relay/Drop flags, while the construction
of actual outgoing messages and the respawn gold-loss result remained outside
the tested boundary. No new gameplay root cause is claimed from this finding.

A small server-only InventoryChangePublication helper now joins the existing
checked mutation with construction and immediate publication of its existing
wire messages. InventoryService and PlayerService supply their original recipient
routing callbacks. The helper retains no callback, pointer or queued work.
Authority lookup, permitted non-owner NPC policy, range checks, arithmetic,
gold-loss calculation, failure logging and the independent NotifyRespawn path
remain in their existing domains. No protocol/schema or native hook changed.

The same five inventory regression cases now exercise this production boundary:

- Authority failures preserve the entire entry list and publish no message:
  missing entity/owner, stale or zero owner epoch, non-owner player, distant NPC
  and unexpected epoch on an unowned object.
- Successful changes apply once before publication and emit zero or one exact
  NotifyInventoryChanges with the requested actor/epoch/item and permitted Drop;
  owner suppression, nearby non-owner relay, unowned objects and disabled/unrequested
  drops are covered.
- Checked-arithmetic failure preserves the entry list and emits no notification.
- Respawn gold success emits one current-epoch delta followed by one personal
  result, with the count already changed and no synchronized drop.
- Missing gold or a rejected zero delta emits no inventory message, preserves
  all entries and sends one personal result with GoldLost=0.

Local Windows x64 release / 1.6.1170: SkyrimServerRunner including STServer
built successfully; focused TPTests passed **151 assertions / 5 cases**;
full TPTests passed **287456 assertions / 1039 cases**. Existing directory-symlink
privilege warnings remain; those unavailable local branches are not newly proven.
Final hosted results for this follow-up are recorded on
[draft PR #20](https://github.com/MrMartexX/TiltedEvolution/pull/20), separately
from the preceding d603fb39 evidence. Native/runtime/content and actual
two-client recipient/ownership behavior remain acceptance gates.

Supplementary read-only CDB inspection of the already preserved exact 1.6.1170
image/dump matched the imported read/write lock entry IDs 68233/68234/68239/68240,
the ExtraDataList lock at 0x18 and GameVMUpdate's inactive read at 0x680.
Output: D:/Codex/Artifacts/upstream-intake-2026-10-28/native-lock-boundary-1.6.1170.txt.
No game was attached or launched for this inspection. This is not complete
IDA/Ghidra, thread-domain, scene-layout or multi-runtime acceptance; unverified
native assumptions remain explicitly unaccepted.

## Completed T1 follow-up and T2 factory coverage

T1 commit f243e95b1c28165e104411c9e9dcc10839b8b8b1 completed all required
hosted checks before starting the independent factory-test follow-up:

| Hosted validation | Assertions / cases |
|---|---|
| [Linux full build 40803069595](https://github.com/MrMartexX/TiltedEvolution/actions/runs/40803069595) | 286480 / 985 |
| [Windows full build 40803069698, both 1.6.1170 and latest](https://github.com/MrMartexX/TiltedEvolution/actions/runs/40803069698) | 287467 / 1039 each |
| [Diagnostics 40803063641, Linux](https://github.com/MrMartexX/TiltedEvolution/actions/runs/40803063641) | 286480 / 985 |
| Diagnostics, Windows, same run | 287467 / 1039 |

Required build/test/UI/staging steps ran. PR build checkout was
0aed683929ff3813fdad144e64f3aad814b5256a; its full tree equals f243e95b.
Full logs are preserved under D:/Codex/Artifacts/upstream-intake-2026-10-28/hosted-f243e95b.
A clean local full 1.6.1170 release build and full TPTests also passed on f243e95b.
Both x64 client and server contain the same git-describe build identity ending
gf243e95b; hashes and local results are in inventory-publication-follow-up/local-f243-validation.json
under that artifacts directory. These new binaries have not acquired gameplay evidence.

The T2 review then found that factory registration/count checks and populated
action fixtures did not exercise all ordinary active types or the imported
ownership/scene fields with nonzero values. The test-only follow-up now round-trips
51 client and 54 server types through the real factories, with distinct high-bit
ownership/expected/rider/mount epochs, actor/owner identities, scene ID/phase,
leveled NPC identity and populated nested ActorData where present. The latter is
checked explicitly because AssignCharacterRequest's existing equality omits it.
Some older user-provided constructors leave scalar fields uninitialized; the
fixtures assign those fields explicitly rather than reading indeterminate values.
No default-constructor behavior or wire schema is changed by these tests.

The six canonical message types retain their existing valid-identity fixtures in
party_quest_protocol/coordinator; two nonempty bounded action fixtures remain in
upstream_protocol_intake. Both new visitor loops assert that exactly four types
use these separate fixtures, so a new decoder-status type cannot silently escape
coverage. Together the suites cover all 113 active registrations and retain
reserved-slot rejection. Existing valid/invalid wire tests are not removed.

Local Windows 1.6.1170 release: the focused encoding.intake tag passed
**559 assertions / 5 cases** and full TPTests passed **287882 / 1041**.
The production/native/protocol/workflow trees still equal f243e95b; only tests and
documentation change. Exact hosted results for the final test commit belong to
[PR #20](https://github.com/MrMartexX/TiltedEvolution/pull/20), not to an inferred
green status from its parent. Native/runtime/content and actual multiplayer
acceptance remain open; canonical mutation stays disabled.

## Required before integration

1. Preserve each exact-SHA hosted record above; require all actual checks and exact counts for the notification follow-up before integration.
2. Keep runtime-separated artifacts paired with their exact source/build identity.
3. Finish native runtime, dialogue/inventory/ownership and content/compatibility checks from T1–T10.
4. Review final source against both merge parents and retain the exclusions above in the merge record.
5. Only then update integration/dev and project acceptance status.

The earlier two-client mutation guide is historical research for this candidate; its environment variable cannot enable canonical mutation.


## Replay reset follow-up — 2026-10-30

A source-level counterexample was found while investigating the two-client
animation report: ActionReplayCache may return ResetAnimationGraph=true with
Actions empty when its final retained action is an exit. AnimationSystem::Update
previously reset only inside the due-action branch and only with ReplayCount>0.
Such a reset-only request could therefore never execute, even after the graph
became ready. This is a demonstrated protocol-consumer mismatch, not proof that
it caused every reported sliding/missing-animation symptom.

The pending reset is now consumed independently of queue length and ReplayCount.
It waits for graph readiness, remains pending if the native reset reports failure,
and retires after successful reset. The existing per-action readiness check runs
after the reset, so reset cannot bypass graph readiness for subsequent playback.
No new action is synthesized; action order/count, wire format, ownership policy,
generation fences and canonical mutation authority are unchanged.

The small callback seam is used directly by the production consumer. Five
regressions cover a real cache-produced exit-only chain, readiness delay and
retry, preservation of subsequent actions, ordinary playback without a reset,
and a failed native reset followed by success. Local latest Windows client/launcher
and TPTests compiled. Focused TPTests: **35 assertions / 5 cases**; full TPTests:
**287917 assertions / 1046 cases**, all passed. Existing local symlink-privilege
warnings remain disclosed. Exact follow-up SHA hosted checks must finish before
integration; these local results do not imply Linux/Windows hosted completion.

Read-only inspection of the exact latest executable (SHA256
846efccf0c1374d71f892907f46549560f2fcb0a75cb87a3eed438baa0f1402f)
through native RTTI/vtables additionally correlates scene owner/state/phase fields
at 0x98/0xB0/0xBC, action-array data/count at 0x80/0x90, package GetType slot 7
returning 1 and Loops slot 5 returning false, phase condition lists at 0/8, and
Character GetCurrentScene slot 0x4A. GameVM fields 0x210/0x690 and the floating
SpeakSound return were independently inspected. These checks are bounded layout/
entry evidence, not a blanket thread-domain or all-runtime authorization.
The mapped-engine crash stack was traced through the quitGame branch into
resource destruction: it is an exit-context failure, with the invalid pointer's
origin still unknown. No speculative teardown patch or additional dump capture
was introduced.

The user explicitly declined a repeat test solely to equalize the friend's CC
plugins (33 vs host 8); retain the mismatch as an evidence limitation rather than
an independent mandatory rerun. Actual owner/epoch observations and remaining
native/thread/lifecycle acceptance must still be reviewed. P0 remains open.


## Ownership playback retirement follow-up — 2026-10-30

After the replay-reset slice c810e7f6 completed every required Linux/Windows
build and diagnostic job, source audit found a second bounded counterexample.
OnOwnershipTransfer rebuilds interpolation for a new remote owner but preserves
an existing RemoteAnimationComponent. Its TimePoints, ReplayCount and pending
graph reset could therefore execute old-owner work under the newly accepted
epoch. Local-owner promotion eventually deletes that component, but the
remote-to-remote and not-ready/decline paths did not retire it first.

The consumer now retires these three queued-playback fields immediately after
the existing nonzero/newer-epoch and entity checks, before reference lookup,
reconciliation or a possible ownership decline. No native graph reset or other
engine action is performed by retirement. Last already-played diagnostic state
and the actual visible graph are left alone. There is no new lifecycle domain,
wire field, authority, opcode or callback ownership change.

The production helper is tested with real ActionEvent lists. Four regressions
cover future actions plus a pending reset, rejected zero/stale/duplicate epochs
preserving the full queue state, reset-only replay retirement, and unknown-epoch/
consecutive handoffs. A new-owner action can subsequently enter a clean queue.
The helper does not authenticate packets still in the network: existing action
messages carry no ownership incarnation, so A-to-B-to-A late arrival remains an
explicit protocol-provenance limitation, not claimed fixed by this slice.

Local MSVC 1.6.1170 client/launcher and TPTests built. Focused tests passed
**31 assertions / 4 cases**; full TPTests passed **287948 / 1050**. Hosted checks
for the new commit must be recorded separately; c810e7f6 remains the previous
fully green reference until they complete. No gameplay proof on the new commit
is claimed and canonical mutation remains disabled.

Exit-dump inspection additionally found that critical allocator/object memory
is absent from the snapshot and stream 16 (memory-information map) is missing.
The invalid read and quit-cleanup call chain are confirmed, but prior free vs
corruption, origin and responsible component cannot be proved from missing
pages. No new dump capture, speculative allocator patch or third-party-mod blame
was introduced.

### Deferred 3D replay copy correction

The same ownership-retirement slice also retires the independent
WaitingFor3D::SpawnRequest.ActionsToReplay copy. RunRemoteUpdates consumes its
first action's variables after 3D becomes ready; clearing only the live playback
queue would allow this old-owner seed to survive the handoff. Both copies now
use the same nonzero/newer-epoch check and are cleared before reconciliation or
an ownership decline. Existing reconciliation of inventory, actor values,
death/draw state and the pending request's epoch is unchanged.

Two additional regressions use the actual ActionReplayChain: both copies are
retired together, and zero/stale/duplicate grants preserve the deferred seed.
Local MSVC latest client/launcher and tests built successfully. Focused tests
passed **44 assertions / 6 cases**; complete TPTests passed **287961 / 1052**.
The local runner reports unavailable directory-symlink privilege; the hosted
Windows counts must be recorded separately. These results cover the corrected
working tree, not gameplay or packets still in flight. Final exact-SHA CI remains
required before acceptance; canonical mutation is still disabled.
