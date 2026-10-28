# Upstream conflict resolution review — 2026-10-28

Status: Evidence snapshot with Planned resolutions. Source review only; no production changes, merge, build acceptance or P0 closure.

This is the detailed follow-up to [the intake inventory](UPSTREAM_DEV_INTAKE_2026-10-28.md). It refines that report's preliminary resolutions, especially the quest-stage intake, SteamStub bypass, opcode numbering and CI. It does not supersede historical CI observations.

## Exact scope and evidence

| Input | Pinned identity |
|---|---|
| Fork integration | `99b4d0803810c22d30f14365b5c94d7fc5dd290c` |
| Fork dev / merge base | `9d81ef07d68e4bb2bd94fca246e798a564b7fb92` |
| Upstream dev | `25a0049a00900b8714e105f795380caf33678012` |
| Diagnostic merge tree | `819a145340f69aac1688d31a30ac181630093f93` |
| Upstream delta | 107 commits; 354 changed paths |
| Overlap | 34 paths: 20 conflicting, 14 textually clean |
| Explicit conflict regions | 36 |

All three remote heads were refreshed again during this follow-up and matched these identities. The diagnostic tree contains unresolved markers: it is neither a runnable candidate nor a proposed final tree.

The [resolution manifest](UPSTREAM_CONFLICT_RESOLUTIONS_2026-10-28.json) maps every conflict region to a decision, including its line interval in that diagnostic tree. These intervals are not line numbers in either parent.

The root license blob is unchanged between the pinned tips. Preserve existing project/component attribution; no code was copied or new dependency license accepted in this review. [Pinned upstream source](https://github.com/tiltedphoques/TiltedEvolution/tree/25a0049a00900b8714e105f795380caf33678012), [pinned fork source](https://github.com/MrMartexX/TiltedEvolution/tree/99b4d0803810c22d30f14365b5c94d7fc5dd290c).

## Recommended integration strategy

Prepare one isolated integration candidate, using the fork as the starting point and preserving upstream history. Resolve related files as connected groups, then validate the candidate before updating either integration or dev. Do not use whole-repository ours/theirs resolution.

Most upstream improvements can be retained. Two behaviors should not enter the first candidate unmodified:

1. **Deferred legacy quest stages from #848.** Keep the current legacy stage behavior initially; port the new held-stage behavior separately after its ownership/completion contract is proven. Keep the useful scene declarations required by dialogue independently. This is a deliberate, documented partial intake of #848, not a claim that the whole feature was accepted.
2. **SteamStub success on zero section metadata.** Keep the fork's explicit flag-based decision. Zero fields alone must not become a second permission to jump to the original entry point. An additional valid binary variant needs exact evidence and a regression fixture.

Do not replace these with silent feature flags that imply a tested implementation. Record retained/deferred upstream behavior in the eventual merge description and future intake ledger; an upstream ancestor will otherwise look fully incorporated to Git even where its behavior was deliberately excluded.

The existing canonical executor remains non-mutating. Legacy STR already calls ScriptSetStage, separately from the canonical executor; preserving that baseline is not proof that legacy quest synchronization meets P0.

## R1 — Inventory: authorize, then apply exactly once

Affected conflicts: server InventoryService.cpp and PlayerService.cpp. Related clean changes: client InventoryService, actor ownership components/messages and Actor.cpp.

The diagnostic inventory tree contains both the fork's checked AddOrRemoveEntry and the upstream unchecked call below the conflict. Combining both arms would also redeclare inventoryComponent. Merely renaming that variable could turn a compile error into a double delta. Taking only the fork arm would apply the delta before the new ownership check.

Use this order:

1. Find the inventory entity once; stop if absent.
2. Validate the sender and OwnershipEpoch before modifying inventory.
3. Preserve upstream's permitted non-owner NPC interaction only when the owner exists, the target is not a player, and the cell/range condition passes. Preserve rejection of unexpected nonzero epochs on unowned objects.
4. Call the fork's checked AddOrRemoveEntry exactly once.
5. On failure, return without notification or further mutation.
6. Apply upstream relay rules: owner-originated UpdateClients=false can suppress relay; permitted non-owner NPC changes still relay to the owner/other clients; remote interaction cannot create a synchronized drop.

Do not strengthen this into a blanket owner-only rule: that would break legitimate pickpocketing and looting. Do not advertise the current range check as a dedicated pickpocket-distance/interaction lease: it is cell visibility/range validation.

For respawn gold loss, retain one checked delta and one notification object inside the success branch. Add the current owner's epoch to that object. Keep GoldLost=0 when the inventory delta fails and preserve the separate respawn notification. No second inventory declaration or unconditional loss notification.

**Required tests (T1):** missing entity, stale epoch, wrong owner/player target, allowed nearby non-owner NPC, disallowed distant NPC, unowned object epoch=0/nonzero, UpdateClients=false behavior, checked-arithmetic rejection, one successful delta/one relay, respawn epoch and failed gold delta. Assert stored counts and outgoing messages, not just function return values.

The naked-NPC ResetInventory loop stays removed. Upstream agrees with that removal; retain the fork's anti-recreation policy and arithmetic tests. Epoch matching does not make a repeated valid delta idempotent: atomic item-transfer IDs remain a separate feature.

Sources: [upstream inventory](https://github.com/tiltedphoques/TiltedEvolution/blob/25a0049a00900b8714e105f795380caf33678012/Code/server/Services/InventoryService.cpp), [fork inventory](https://github.com/MrMartexX/TiltedEvolution/blob/99b4d0803810c22d30f14365b5c94d7fc5dd290c/Code/server/Services/InventoryService.cpp), [fork respawn](https://github.com/MrMartexX/TiltedEvolution/blob/99b4d0803810c22d30f14365b5c94d7fc5dd290c/Code/server/Services/PlayerService.cpp).

## R2 — Opcodes and factories: explicit compatibility decision

Affected conflicts: Opcodes.h and both factory headers. Required companion change if reserving removed values: both factory .cpp extraction functions.

The upstream change is already a wire change: ownership epochs and dialogue identities alter existing payloads; action messages are new. It also removes CancelAssignmentRequest, NotifySpawnData and NotifyRelinquishControl. Their removal shifts implicit enum values even outside the marked conflicts.

Recommended numbering: preserve every surviving fork numeric opcode, reserve the removed slots, and append new actions after the current fork range.

| Entry | Fork number | Recommended candidate number |
|---|---:|---:|
| Removed client CancelAssignment | 1 | reserved/rejected |
| RequestPartyQuestTransaction | 52 | 52 |
| RequestPartyQuestReplicaReport | 53 | 53 |
| RequestPartyQuestRepairAck | 54 | 54 |
| RequestActionEvents | absent | 55 |
| Client maximum sentinel | 55 | 56 |
| Removed server NotifySpawnData | 16 | reserved/rejected |
| Removed server NotifyRelinquishControl | 44 | reserved/rejected |
| NotifyPartyQuestTransactionResult | 56 | 56 |
| NotifyPartyQuestRepairPlan | 57 | 57 |
| NotifyPartyQuestCanonicalUpdate | 58 | 58 |
| NotifyActionEvents | absent | 59 |
| Server maximum sentinel | 59 | 60 |

Make values/reservations explicit and assert the important values. Do not reuse a removed slot for a different message. Factories include/register all surviving messages plus PartyQuest and actions; remove registrations/includes for deleted types, including NotifyRelinquishControl left in a conflicted visitor arm.

**Necessary companion guard:** current Extract checks only opcode < Max and calls the std::function array entry. A reserved slot has an empty function; calling it throws bad_function_call inside noexcept and can terminate the process. Reject an unregistered slot before invocation in both factories. Reserved-slot rejection is part of this resolution, not an optional later cleanup.

Unchanged authentication messages retain opcode zero and their existing payload. The server compares Version with BUILD_COMMIT before admitting a player. Preserve that gate and ship matching candidate client/server builds. Stable opcode values do not make the changed payloads compatible with the old fork or stock upstream. BUILD_COMMIT identifies a build convention, not a cryptographic schema/dirty-tree proof.

**Required tests (T2):** full active-factory round trips; exactly one registration per active opcode; all three reserved slots and out-of-range bytes return rejection without a native exception; PartyQuest values/schema stay stable; action messages round-trip; mixed old/new builds are rejected at authentication; existing changed ownership/dialogue payloads round-trip on both endpoints.

Do not copy upstream instruction-file advice permitting arbitrary opcode reorder into the fork's architecture. No PartyQuest schema expansion is required to resolve these conflicts.

Sources: [fork opcodes](https://github.com/MrMartexX/TiltedEvolution/blob/99b4d0803810c22d30f14365b5c94d7fc5dd290c/Code/encoding/Opcodes.h), [upstream opcodes](https://github.com/tiltedphoques/TiltedEvolution/blob/25a0049a00900b8714e105f795380caf33678012/Code/encoding/Opcodes.h), [factory extraction](https://github.com/MrMartexX/TiltedEvolution/blob/99b4d0803810c22d30f14365b5c94d7fc5dd290c/Code/encoding/Messages/ClientMessageFactory.cpp), [authentication gate](https://github.com/MrMartexX/TiltedEvolution/blob/99b4d0803810c22d30f14365b5c94d7fc5dd290c/Code/server/GameServer.cpp).

## R3 — Quest service: preserve two distinct authority paths

Affected conflicts: client QuestService.cpp/h and server QuestService.cpp. Related unmarked changes: TESQuest.cpp/h, EventDispatcher declarations and server PartyService::Party::QuestStages.

Keep the fork's canonical protocol handlers, campaign/session identities, compatibility cache, submission tracking, canonical inbox, runtime owner/deferred queue routing and dry-run boundary.

For the first intake candidate:

- Retain the current client legacy quest implementation and its two existing engine sinks. Do not add the upstream HeldStages storage, OnUpdate consumer, five extra sinks or StageItemDone callback merely to make the combined header compile.
- Retain the fork's exact dispatcher pointers and explicit destructor. Keep one disconnected/party-left connection, plus party-joined and canonical connections. Do not introduce the duplicate m_leftConnection/m_partyLeftConnection pair.
- Keep the upstream scene type declarations needed by dialogue (R4). They do not require registering all the new QuestService debug sinks.
- Defer the #848 server QuestStages dedup/reset block and its Party member together with the client behavioral port. Accept the harmless misc-quest debug-level logging change, preserving fork canonical server handlers and persistence.
- An optional outside-party overlay hint can be ported independently, with null/lifetime checks. It is not a prerequisite for this intake.
- Rewrite the class comment to distinguish legacy stage relay from canonical dry-run. Do not state that a party leader owns canonical campaign truth.

### Why adding generation to the map is insufficient

Upstream m_heldStages is keyed only by local FormID. HeldStages contains a sorted list of stage numbers and a second list with one stage number per pending log-entry fragment.

Specific gaps:

1. OnDisconnected and OnPartyLeft clear it; LoadGame/NewGame/MainMenu and campaign changes are not bound into the item. The same FormID after loading can name a different quest instance.
2. The stage-item callback queues a lambda capturing this. It copies only quest FormID/stage. A delayed event can retire pending data from a later instance, even if the old map was cleared.
3. TESQuestStageItemDoneEvent exposes formId, stageId and an unknown bool, not a demonstrated unique log-entry/operation token. For two pending entries at the same stage, a duplicate delivery is indistinguishable from a second completion in this representation. This is a counterexample to an idempotency proof, not a claim that the engine has been observed duplicating that event.
4. Counting defined log entries does not by itself prove that every entry's fragment executed or that all other Papyrus side effects completed. Return from ScriptSetStage only checks IsStageReached, not full postconditions.
5. NotifyQuestUpdate carries GameId, stage, status and quest type. It has no authoritative transaction, expected revision, campaign or quest-instance identity. A locally invented ID cannot reconstruct those absent server facts.
6. Leader change is not invalidation in the held-stage consumer. A former leader can retain leader-specific work unless the adapted design explicitly revokes it.
7. Numeric stage sorting and per-party stage sets are legacy heuristics; they do not prove canonical transaction ordering or correctness for repeated quest instances.
8. Registration cleanup is not callback quiescence. PartyQuestReleaseExternalSink unregisters an exact pointer; it does not prove that all in-flight callbacks or runner tasks have finished.

The runner drains before UpdateEvent, but its exposed Queue/Drain interface has no per-owner cancellation or joined callback token. Ordering within one update is not a cross-thread lifetime proof.

### Concrete route for the later port

Use a QuestService-owned legacy queue with immutable session incarnation, process runtime generation, party/leadership incarnation, server quest GameId, resolved local quest instance and a local admission sequence. Bind campaign/revision only when obtained from a verified canonical source; never synthesize canonical permission from a legacy packet.

Use the existing process generation fence through consumption, with late lookup and current mapping/instance revalidation. Queue mutation, authority transitions and consumption must have a documented serialization domain. A generation check released before execution is insufficient. Do not hold a lease indefinitely while waiting for Papyrus fragments.

Native callbacks need an owner token whose lifetime outlives queued work, a stopped-ingress state and in-flight retirement before owner destruction. Cancellation after LoadGame must discard stale work, not look up the reused FormID and perform cleanup mutations on its new object.

Treat StageItemDone/scene events as evidence. Use an independently verified completion observer/envelope for completion; do not equate a decremented counter with full authorization or quiescence. If that observer is unavailable, deny deferred execution and report the reason. Full canonical use should consume the existing PartyQuest transaction/revision protocol, not extend legacy packets casually.

**P0-C dependency:** an externally registered engine callback must be prevented from entering a destroyed service, and queued work must not dereference the retired owner. An internal shared token alone cannot repair an already dangling engine sink pointer.

**Required tests (T3):** generation change, disconnect/reconnect with reused player ID, leave/recreate party, campaign A/B with same quest ID, LoadGame/NewGame/MainMenu, leader transfer, quest stop/restart with same stage, duplicate enqueue/readiness/completion, late callback into a new instance, observer unavailable, failed revalidation leaves state unchanged, invalidation racing consume, and shutdown with pending callbacks. No test may use a successful readiness lookup as authority.

Sources: [upstream held-stage implementation](https://github.com/tiltedphoques/TiltedEvolution/blob/25a0049a00900b8714e105f795380caf33678012/Code/client/Services/Generic/QuestService.cpp), [event payload](https://github.com/tiltedphoques/TiltedEvolution/blob/25a0049a00900b8714e105f795380caf33678012/Code/client/Games/Skyrim/Events/EventDispatcher.h), [fork service](https://github.com/MrMartexX/TiltedEvolution/blob/99b4d0803810c22d30f14365b5c94d7fc5dd290c/Code/client/Services/Generic/QuestService.cpp), [generation fence](https://github.com/MrMartexX/TiltedEvolution/blob/99b4d0803810c22d30f14365b5c94d7fc5dd290c/Code/encoding/Structs/Skyrim/PartyQuestRuntimeGenerationFence.h), [runner](https://github.com/MrMartexX/TiltedEvolution/blob/99b4d0803810c22d30f14365b5c94d7fc5dd290c/Code/client/Services/Generic/RunnerService.cpp).

## R4 — TESQuest ABI and compatibility evidence

The header conflict is not just an include union. Preserve Forms/BGSBaseAlias.h and typed GameArray<BGSBaseAlias*> aliases at 0x58. Keep all fork sizeof/offset assertions.

The expanded BGSScene/BGSScenePhase/BGSSceneAction declarations are also used by upstream Actor dialogue and CharacterService scene suppression. They must be considered with those callers, even if held quest stages are deferred. Assert the scene/action offsets and verify them on each admitted binary; successful C++ compilation alone proves only internal consistency.

The upstream stage work changes:

- GameList<Stage> to GameValueList<Stage*>;
- Stage to expose items at 0x8;
- IsStageDone from a flag walk to Address Library function 25011;
- ScriptSetStage from void to bool and adds IsStageReached/GetStageItemCount.

The two list head representations contain two pointer-width fields, but Empty/iteration behavior differs: GameList treats a null head data pointer as empty; GameValueList also checks the next pointer. Existing topology hashing counts list nodes and rejects null stage entries. A null-head/nonempty-tail fixture is therefore necessary; layout similarity is not evidence of identical fingerprints.

**First-candidate decision:** take the scene declarations needed by dialogue while preserving the fork's typed aliases, stage representation and legacy TESQuest.cpp stage methods. Do not partially change the ScriptSetStage return contract while deferring its held-stage consumer. Import the stage representation/engine helper changes together with their tests and runtime proof in the later port.

Review these fork-only callers before that port: QuestSnapshotCollector, PartyQuestP0LiveDiagnostics, HashResolvedQuestTopology, and the canonical executor's ContainsStage. Snapshot CompletedStages currently comes from Stage::IsDone flags; switching one query to an engine helper does not automatically update or prove the snapshot's semantics.

The new SkyrimTogetherQuestPatches.esp also changes plugin content independently of this C++ header. Generate new compatibility evidence for the intended plugin list; do not edit expected digests merely to make mismatches disappear. No alias mutation is introduced.

**Required tests/evidence (T4):** empty/single/multiple/null-head stage lists; alias typing; sizeof/offsets for both builds; unchanged valid topology fingerprints or explicitly reviewed changed results; real quest stage/scene observations on each supported runtime; canonical executor still has no native SetStage side effect.

Sources: [upstream TESQuest](https://github.com/tiltedphoques/TiltedEvolution/blob/25a0049a00900b8714e105f795380caf33678012/Code/client/Games/Skyrim/Forms/TESQuest.h), [upstream methods](https://github.com/tiltedphoques/TiltedEvolution/blob/25a0049a00900b8714e105f795380caf33678012/Code/client/Games/Skyrim/Forms/TESQuest.cpp), [fork topology observer](https://github.com/MrMartexX/TiltedEvolution/blob/99b4d0803810c22d30f14365b5c94d7fc5dd290c/Code/client/Games/Skyrim/PartyQuestSkyrimRuntimeCompatibilityEvidence.cpp).

## R5 — GameVM, main-loop fences and runtime admission

Resolve both SkyrimVM64.cpp regions as one type-consistency change:

- Keep the fork Main definition and resetGame offset assertion.
- Keep PartyQuestSkyrimMainLoopLifecycleHookInstaller, ObserveCurrentUpdateThread and the pre/post MainMenu transition.
- Remove the independent hard-coded VMContext.
- Use the upstream GameVM type consistently in TVMUpdate, HookVMUpdate and ThisCall.
- Include the same runtime-conditioned GameVM.h everywhere; no second local structure with a different offset.

Upstream defines virtualMachine/inactive at 0x200/0x680 for TP_SKYRIM_1_6_1170, versus 0x210/0x690 for latest. This affects the fork Papyrus observer too: it dereferences GameVM::Get()->virtualMachine although that source file has no textual conflict.

Build both client configurations explicitly. For the installed test environment, select --skyrim_version=1.6.1170; never rely on upstream's latest default. Separate build directories/artifacts/cache identities and do not reuse a PCH/object output from the other configuration.

Add the upstream numeric-runtime rejection before Address Library initialization and before any hook installation. Keep the fork native target validation before TP_HOOK_COMMIT and detour validation afterward. The numeric allowlist is only a first filter: exact executable identity, database identity and native profile checks remain required.

Upstream's older build also lists GOG 1.6.1179. That is upstream admission, not local P0 evidence. The fork currently restricts production Papyrus generation/snapshot authorizations to the verified 1.6.1170 path; 1.7.104 retains a diagnostic surface without granting an unregistered production profile. Preserve those distinctions.

**Required tests/evidence (T5):** both MSVC runtime builds; wrong-runtime executable rejected before hooks/SKSE; GameVM offsets for both configurations; MainMenu invalidates before reset; Papyrus worker callback cannot establish the mutation thread identity; missing/wrong executable profile remains denied; live start/load/return-to-menu on explicitly named artifact/runtime pairs.

Sources: [upstream GameVM](https://github.com/tiltedphoques/TiltedEvolution/blob/25a0049a00900b8714e105f795380caf33678012/Code/client/Games/Skyrim/Misc/GameVM.h), [fork main-loop](https://github.com/MrMartexX/TiltedEvolution/blob/99b4d0803810c22d30f14365b5c94d7fc5dd290c/Code/client/SkyrimVM64.cpp), [fork observer](https://github.com/MrMartexX/TiltedEvolution/blob/99b4d0803810c22d30f14365b5c94d7fc5dd290c/Code/client/Games/Skyrim/PartyQuestSkyrimPapyrusRuntimeObserver.cpp).

## R6 — Address Library, SKSE and startup

### VersionDb.h: retain the fork implementation as a unit

Resolve both regions by retaining the fork loader/parser integration, not selected lines inside the upstream LoadV2/LoadV5 skeleton. The diagnostic merge nests fork runtime-variable code into a differently scoped upstream decoder.

The fork already supports formats 1/2/5 through its deterministic parser, bounds the database input, checks runtime/module/pointer width, stages maps before publication and rejects the incompatible SE ID namespace at the AE call-site loader. Upstream format-5 support is not a reason to replace this implementation. Keep public methods consumed by fork native observers and validation.

Do not introduce automatic preference/renaming of -0-1 databases. Numeric runtime equality is not a unique executable identity.

### ScriptExtender.cpp: retain checks, adopt rename and accurate logs

Keep the fork's exact runtime DLL selection, correct trailing-zero formatting, filesystem error handling, missing-export cleanup and repeated-call guard. Resolve the conflict inside exeVersion.empty as the existing error-and-return.

Upstream's start-hook log lines belong beside the real pStartSKSE call after module/export validation; the diagnostic conflict places them before skseVersion/pStartSKSE exist. Use wording that startup hooks were installed and initialization continues, not that every plugin/bridge is ready.

Rename LoadScriptExender to LoadScriptExtender in declaration, definition and its one early call. Do not introduce a second call in BeginMain. Preserve the surrounding pre-entry exception containment and game-entry integrity checks.

### main.cpp and TiltedOnlineApp.cpp

Keep both independent error dialogs: incompatible build/runtime and invalid native hook. Retain PartyQuestSkyrimNativeHookValidation include and all validation calls.

Retain the three fork includes in TiltedOnlineApp.cpp and the existing P0 bootstrap/EndMain order. The upstream per-instance log sink can be adopted independently; verify distinct log files for simultaneous clients and preserve process-lifetime slot ownership. Removal of the obsolete ScriptExtender include does not remove the early loader.

**Required tests/evidence (T6):** existing parser malformed/truncation/oversize/format/runtime/namespace cases; rejected candidate publishes no partial maps; missing/mismatched SKSE export/DLL does not imply readiness; one StartSKSE call; hook-target and post-hook integrity failures stop game entry; per-instance diagnostics remain distinguishable.

Sources: [fork VersionDb](https://github.com/MrMartexX/TiltedEvolution/blob/99b4d0803810c22d30f14365b5c94d7fc5dd290c/Code/client/VersionDb.h), [fork loader](https://github.com/MrMartexX/TiltedEvolution/blob/99b4d0803810c22d30f14365b5c94d7fc5dd290c/Code/client/ScriptExtender.cpp), [upstream startup](https://github.com/tiltedphoques/TiltedEvolution/blob/25a0049a00900b8714e105f795380caf33678012/Code/client/main.cpp).

## R7 — SteamStub: clean merge, different decision

This file has no conflict marker but requires an explicit resolution.

The fork tests HasEncryptedCodeSection solely from the corrected Flags field. Upstream adds another early return when CodeSectionVirtualAddress==0 OR CodeSectionRawSize==0. These conditions are **not equivalent**.

| Signature/OEP valid | NoEncryption | Section metadata | Fork decision | Additional upstream branch |
|---|---|---|---|---|
| no | any | any | reject | must not bypass rejection |
| yes | set | any | skip decryption | no additional value |
| yes | clear | both nonzero | decrypt | unchanged |
| yes | clear | either zero | decrypt policy | returns OEP without decryption |

The existing synthetic fork header tests have Flags=0 and zero section fields. They test the helper, not CrackCEGInPlace; they would not detect this new call-site bypass.

Best first intake: retain the fork implementation, including SteamAppId/Flags offsets, signature/OEP rejection and entry-point integrity checks; omit the new zero-field success branch. Add a call-site decision fixture before considering any alternate unencrypted variant. For a newly observed contradictory header, stop for exact variant validation; do not treat zero metadata as sufficient proof of executable plaintext.

No new game launch or decrypted binary evidence was collected here. This review does not establish which contradictory combinations occur in real installations.

**Required tests/evidence (T7):** every row above, both single-zero cases, corrected AppId alignment, actual restoration path taken, invalid signature/OEP, source/mapped entry integrity. Do not change the synthetic tests to mask a weaker bypass.

Sources: [fork header/policy](https://github.com/MrMartexX/TiltedEvolution/blob/99b4d0803810c22d30f14365b5c94d7fc5dd290c/Code/immersive_launcher/steam/SteamCrypto.h), [upstream call site](https://github.com/tiltedphoques/TiltedEvolution/blob/25a0049a00900b8714e105f795380caf33678012/Code/immersive_launcher/steam/SteamCeg.cpp), [fork regression](https://github.com/MrMartexX/TiltedEvolution/blob/99b4d0803810c22d30f14365b5c94d7fc5dd290c/Code/tests/party_quest_steam_ceg_policy.cpp).

## R8 — Build and test sources

Root xmake.lua already pins tiltedcore 0.2.9 in the fork. This is not a pending upgrade from an older fork version. Resolve that marker by retaining one root declaration and removing unused recastnavigation after the reference search (no code/build references beyond that fork declaration were found).

Remove the redundant client-level add_requires("tiltedcore"), retain one build_client invocation, add the runtime define, and preserve fork source globs/syslinks including shell32/bcrypt. Upstream removes optional Vivox setup; no retained upstream build/source references were found in the inspected tree. Remove stale blocks as a group, not just the requirement.

Code/tests/xmake.lua needs a union:

- Keep all five Windows NativeLoadBridge source files, TPTests.rc, kernel32 and bcrypt under the Windows condition.
- Add PlaybackClock.cpp outside that condition for both platforms.
- Keep the full *.cpp test glob and fork client/launcher include paths.
- Keep Catch2's installed header/API. Do not split off or silently exclude old tests to accommodate new ones.

Preserve dependency pins for mimalloc, CMake, OpenSSL, zlib, protobuf and Linux libcurl unless separately proven incompatible. Keep Windows GameNetworkingSockets built from the pinned source dependency graph.

**Required tests (T8):** full TPTests including Windows-only native bridge suites and new playback-clock cases; list/enumerate cases to verify registration; local configure/build failures remain failures. Linux cannot validate the Windows source/link branch.

## R9 — CI: merge the job contract, not just YAML regions

Affected conflicts: Linux workflow, Windows workflow (six regions), playable wrapper. Related unmarked files: equal-party diagnostics, release workflow, Docker workflow.

### Linux

For the initial required x64 gate retain Ubuntu 22.04 and the fork's pinned protobuf 3.12.3/generator arrangement. The root configuration explicitly depends on that generator/library line; moving the job to Ubuntu 24.04 and accepting its default protoc without checking compatibility is not a mechanical runner update.

Adopt xmake 3.1.0 consistently across full builds and diagnostics, with new cache keys. Preserve config-failure logs and nonzero exit status. Replace removed matrix.mode references with the actual BUILD_MODE; do not leave a blank --mode or misleading cache suffix.

Scope optimization can remain for ordinary changes, but every merge-candidate validation must force full build execution. Stage/install/config/package steps must share the same full-build condition; the diagnostic tree gates compilation but leaves staging unconditional.

Add Ubuntu 24.04/arm64 as a separate toolchain migration only after choosing and verifying a matching protoc plus protobuf runtime for that platform. It must not silently replace the accepted x64 test gate. Do not enable the six-asset release job until both Linux variants actually exist.

### Windows

Use one explicit shell contract. Upstream defaults to Bash, so keep Bash environment writes in Bash steps; leave the scope classifier and SKSE bridge staging explicitly shell: pwsh. Never carry the fork's $env:GITHUB_ENV write into a Bash step.

Pass --plat=windows, architecture, mode and explicit skyrim_version. Build and validate both runtime variants. Keep the fork SKSE lineage bridge staging and its missing-file checks.

Give reusable workflow concurrency groups the runtime and mode (or use job-level matrix-aware grouping). The auto-merged workflow retains the fork's workflow/ref-only concurrency key while the new playable wrapper invokes latest and 1.6.1170 in parallel: both calls would compute the same group and can cancel one another.

Runtime/mode must also identify artifacts and compiled output/cache state. Package dependency caches may be shared only where independent of these defines; do not share stale client/PCH build outputs.

Keep the full-build condition consistently on dependency/cache/config/build/install/server-config/UI/stage/package actions. On candidate acceptance, force it true. Diagnostic TPTests remain unconditional, with their output printed into permanent logs, exact assertion/case counts and propagated exit status.

### Packaging

Adopt packaging in windows.yml and remove the old package-and-upload job in the playable wrapper. Do not retain a consumer of internal-job-files after its producer has moved. Remove str-version output plumbing only after checking all callers.

Preserve sanitization of branch/tag-derived artifact names. Include runtime and exact candidate SHA in the short-lived live-validation artifact name/metadata.

Upload/copy live artifacts before any packaging step that moves the build tree, or package from a copy. Otherwise later upload can see an empty STR_BUILD_DIR. Stage the lineage bridge into the Data-relative SKSE/Plugins layout before packaging.

Keep manual dispatch. Do not turn publication on as part of source conflict resolution. Preserve fork schedule restrictions and inspect Docker registry/release destinations separately.

**Required checks (T9):** YAML/action validation; explicit Bash/PowerShell execution review; no references to removed outputs/matrix fields; simultaneous runtime calls do not cancel each other; full required client/server/test steps run; archive contains correct runtime client, matching server, UI and bridge; all failures propagate; logs retain exact TPTests counts.

Sources: [upstream Windows workflow](https://github.com/tiltedphoques/TiltedEvolution/blob/25a0049a00900b8714e105f795380caf33678012/.github/workflows/windows.yml), [fork Windows workflow](https://github.com/MrMartexX/TiltedEvolution/blob/99b4d0803810c22d30f14365b5c94d7fc5dd290c/.github/workflows/windows.yml), [playable matrix](https://github.com/tiltedphoques/TiltedEvolution/blob/25a0049a00900b8714e105f795380caf33678012/.github/workflows/windows-playable-build.yml), [fork dependency pins](https://github.com/MrMartexX/TiltedEvolution/blob/99b4d0803810c22d30f14365b5c94d7fc5dd290c/xmake.lua).

## R10 — Cleanly merged paths with semantic dependencies

All 14 overlapping paths without textual conflicts were included in the review:

| File(s) | Resolution / acceptance condition |
|---|---|
| docker-server-image.yml | Keep fork publication restrictions; review upstream registry/tag destinations. No release action in this task. |
| Games/Animation.cpp | Accept removal of obsolete actor-specific debug code; preserve fork behavior outside the removed debug block. |
| Games/Skyrim/Actor.cpp | Combine ownership/inventory changes with R1; keep fork anti-reset and diagnostics. Verify changed SpeakSound return ABI and scene offsets with R4. |
| Games/Skyrim/Misc/BSScript.h | Accept new intrusive-refcount header with its dependencies; preserve fork native/Papyrus containment. This include is not a reason to rewrite observer locks. |
| Services/DiscoveryService.h and Generic/DiscoveryService.cpp | Keep fork external-sink lifetime handling while accepting discovery retention needed for actor reconciliation. Do not interpret a loaded reference as authority. |
| Services/InventoryService.h and Generic/InventoryService.cpp | Preserve fork anti-recreation policy/diagnostics; adopt epoch checks and notify-to-owner path with R1. |
| Generic/ObjectService.cpp | Accept personal jail/confiscation-container exclusion with TESFaction/ModManager declarations. Local pointer comparison is not persistent identity. |
| Generic/PartyService.cpp | Accept invitation/timer reset on disconnect; retain fork lifecycle invalidation. |
| Generic/TransportService.cpp | Adopt new send flags/receive-time APIs together with TiltedConnect revision; preserve fork disconnect and session fences. |
| immersive_launcher/steam/SteamCeg.cpp | Use R7's explicit decision, not the automatic merge. |
| Code/xmake.lua | Remove obsolete admin_protocol include with the removed targets; preserve fork targets. |
| README.md | Retain project-specific entry points/documentation; do not claim upstream gameplay features or runtime profiles accepted before their gates. |

Three additional connected areas need explicit acceptance work even though they are not among the 20 conflicts:

### Delayed leveled-NPC reconciliation

CharacterService's pending map stores local reference FormID -> chosen base FormID. It waits for asynchronous disable, performs ApplyPick, then enables/waits for 3D. The loading-menu check delays work; it does not revoke old work after LoadGame.

Adapt the pending value to carry the existing process generation, connection incarnation, server actor identity and OwnershipEpoch, resolved reference identity, desired pick and request incarnation. Revalidate at every phase, not only enqueue. A superseding pick must replace the intended work without reusing permission from the old owner.

Disconnect cleanup currently looks up FormIDs and may EnableImpl. After a generation switch, cleanup must not enable a newly loaded/reused actor. Track ownership of any disable and define cancellation while the original identity remains valid; invalidation of an old generation must not perform generic cleanup on the new one.

Do not drop only the queue while retaining call sites that assume actor conformance. Treat discovery retention, ownership, actor template handling and cancellation as one connected adaptation. If it cannot be proven, keep that behavior out of the accepted intake candidate and record the exclusion.

### Reliable action stream

Both new action decoders resize vectors directly from wire counts and use an unbounded outer count. Add bounded decoding with a total action budget, checked integer conversion and deterministic malformed/truncated rejection before acceptance. Preserve wire encoding if bounds can be enforced locally.

Ownership validation at the server's current OwnerView is useful but not a full epoch binding: the action payload contains server IDs/actions without an ownership incarnation. Do not claim that the ownership-epoch fix automatically covers this stream. Clearing playback buffers on authority changes helps locally but cannot reconstruct missing sender-side history. A wire addition needs a separate explicit protocol decision and proof.

Also, OnActionEventsRequest relays acceptedActions but appends the original actions to ActionsReplayCache, including actions canceled by the script hook. Use the same accepted set for relay and replay cache, with a regression that canceled actions cannot reappear on later spawn/replay. This is a concrete source-level gap, not a live reproduction.

### Engine locks and animation references

Adopt BSAtomic/ExtraDataList read/write locking as a coherent set. Do not mix a recursive spin-lock algorithm with read/write-lock memory. Verify relocation IDs 68233/68239/68234/68240 and offset assertions on the runtime being shipped. Keep the Papyrus observer's separately evidenced RawSpinLock unchanged unless its own contract requires a change.

These checks do not prove every pre-existing concurrency issue solved. In particular, successful compile and same-sized lock fields are insufficient.

**Required checks (T10):** canceled actions excluded from replay; bounded/malformed action payloads; old ownership/generation cannot apply queued actor changes; loading/disconnect during each reconcile phase; preserved anti-reset inventory behavior; appropriate lock-pair/lifetime tests and live gameplay transitions.

Sources: [upstream character service](https://github.com/tiltedphoques/TiltedEvolution/blob/25a0049a00900b8714e105f795380caf33678012/Code/client/Services/Generic/CharacterService.cpp), [server action acceptance](https://github.com/tiltedphoques/TiltedEvolution/blob/25a0049a00900b8714e105f795380caf33678012/Code/server/Services/CharacterService.cpp), [action decoder](https://github.com/tiltedphoques/TiltedEvolution/blob/25a0049a00900b8714e105f795380caf33678012/Code/encoding/Messages/RequestActionEvents.cpp), [engine locks](https://github.com/tiltedphoques/TiltedEvolution/blob/25a0049a00900b8714e105f795380caf33678012/Code/client/Games/Skyrim/BSCore/BSAtomic.cpp).

## Complete marked-conflict checklist

H numbers refer to order within each file, not independent implementation slices. Detailed per-hunk decisions and diagnostic line intervals are in the resolution manifest.

| File | Regions | Recommended disposition | Checks |
|---|---:|---|---|
| .github/workflows/linux.yml | 1 | R9: fork x64 dependency baseline plus reviewed upstream build mechanics; no accidental 24.04/protoc swap | T8, T9 |
| .github/workflows/windows-playable-build.yml | 1 | R9: use reusable runtime matrix; remove obsolete second packaging job | T9 |
| .github/workflows/windows.yml | 6 | R9: scope consistency, explicit runtime, correct shell, runtime-isolated concurrency and one packaging owner | T5, T9 |
| Code/client/Games/Skyrim/Forms/TESQuest.h | 1 | R4: typed aliases + required scene declarations; defer stage behavioral bundle | T3, T4 |
| Code/client/ScriptExtender.cpp | 1 | R6: preserve empty-version failure; relocate accurate startup logs to validated call site | T6 |
| Code/client/Services/Generic/QuestService.cpp | 3 | R3: preserve fork service/registration/canonical routing; defer held-stage activation | T3 |
| Code/client/Services/QuestService.h | 4 | R3: fork ownership/interface/destructor; clarify two authority paths | T3 |
| Code/client/SkyrimVM64.cpp | 2 | R5: typed runtime-dependent GameVM plus all fork MainMenu/thread fences | T5 |
| Code/client/TiltedOnlineApp.cpp | 1 | R6: preserve P0 includes/lifecycle; adopt independent log-slot improvement | T6 |
| Code/client/VersionDb.h | 2 | R6: fork parser/publication implementation as one unit | T6 |
| Code/client/main.cpp | 3 | R5/R6: two error paths, runtime rejection before hooks, both hook validators, one renamed SKSE call | T5, T6 |
| Code/client/xmake.lua | 1 | R8: one root dependency declaration, one client target, explicit runtime config | T5, T8 |
| Code/encoding/Messages/ClientMessageFactory.h | 1 | R2: surviving messages + PartyQuest + action, no removed types; extraction guard companion | T2 |
| Code/encoding/Messages/ServerMessageFactory.h | 2 | R2: include/visitor union with removed types eliminated; extraction guard companion | T2 |
| Code/encoding/Opcodes.h | 2 | R2: stable fork values, reserved retired slots, appended action opcodes | T2 |
| Code/server/Services/InventoryService.cpp | 1 | R1: one lookup, authorization first, one checked delta, success-only relay | T1 |
| Code/server/Services/PlayerService.cpp | 1 | R1: one checked gold delta/notification with current epoch | T1 |
| Code/server/Services/QuestService.cpp | 1 | R3: adopt debug logging only; preserve canonical server and defer associated dedup bundle | T3 |
| Code/tests/xmake.lua | 1 | R8: union Windows bridge sources with portable PlaybackClock source | T8 |
| xmake.lua | 1 | R8/R9: remove unused recast; retain existing tiltedcore version and pinned dependency graph | T8, T9 |

## Implementation sequence and completion boundary

1. Revalidate the current integration tip alone. Its current build/test acceptance is not established by the old green ancestor; obtain exact Linux/Windows counts.
2. Create an isolated intake branch at the refreshed integration head. Preserve the existing working copies and user changes. Merge the pinned upstream only there.
3. Resolve build/runtime/loader and factory/enum interfaces, including guard changes outside marked files.
4. Resolve inventory/ownership and coherent clean-merge dependencies. Apply the intentional #848/SteamStub exclusions. Preserve all P0 recovery/checkpoint/authorization stores and tests.
5. Cover T1–T10 as relevant to the admitted behavior. Source review of deferred exclusions does not require enabling them to run a test. Any compiler correction remains part of this candidate.
6. Run actual Linux build + diagnostics and Windows client/launcher/server + diagnostics with explicit runtime configurations. Record exact TPTests assertions/cases per platform/configuration and artifact SHA. Check required steps were not scope-skipped.
7. Run isolated live validation appropriate to admitted native changes: startup, save/load/menu transition, dialogue/courier/follower/guards, inventory theft/loot, owner handoff, two-client reconnect and NPC reconciliation. Do not test save repair or canonical mutation by enabling it.
8. Review the final diff against both parents and list deliberate exclusions. Only then integrate the candidate and coordinate any fast-forward of fork dev/PR #1's base.

One final merge is possible, but it is not equivalent to choosing 20 conflict sides and trusting CI. Outstanding native/runtime facts require exact evidence. The report recommends a concrete route for each conflict; it does not claim those implementations or runtime proofs already exist.

## Verification performed for this report

- Re-read both pinned versions and diagnostic conflict regions; traced related producers/consumers and factory dispatch.
- Rechecked live remote SHAs and the 20-file / 36-region conflict inventory.
- Enumerated opcode numeric values from both parents and identified reserved-slot extraction behavior.
- Traced inventory delta/relay ordering, legacy completion payloads, GameVM consumers, SKSE call sites and build/package shell conditions.
- Compared SteamStub branch conditions and existing regression coverage.
- Reviewed all 14 overlapping auto-merged paths and the connected action/reconciliation/lock changes.
- Documentation validator passed: 42 Markdown files and 15 ordered P0 task specifications.
- Manifest validation passed: all 20 files / 36 exact conflict intervals covered once, with valid companion paths and test-group references.
- git diff --check passed; working-tree changes are confined to docs/research. Integration/dev references were not changed.

No C++ patch, build, TPTests run, CI run, commit, push, branch update or Skyrim launch is claimed. All T1–T10 entries are acceptance work for implementation, not tests run in this documentation-only task.

**P0 NOT CLOSED.**

