# Upstream intake implementation — 2026-10-28

Status: Local implementation; not accepted. P0 NOT CLOSED.

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

## Required before integration

1. Re-enable fork Actions and execute all required real Linux/Windows jobs on the final candidate.
2. Capture exact test counts, actual client/server build steps and runtime-separated artifacts.
3. Finish native runtime, dialogue/inventory/ownership and content/compatibility checks from T1–T10.
4. Review final source against both merge parents and retain the exclusions above in the merge record.
5. Only then update integration/dev and project acceptance status.

The earlier two-client mutation guide is historical research for this candidate; its environment variable cannot enable canonical mutation.
