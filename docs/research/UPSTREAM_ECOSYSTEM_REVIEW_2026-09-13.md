# Upstream and ecosystem review — 2026-09-13

Status: Evidence snapshot

This report records a read-only review of the official Tilted Evolution
repository, its issue tracker and pull requests, active forks, and selected
Skyrim Together add-ons. It recommends intake priorities but grants no runtime
authority and accepts no external implementation.

## Exact comparison point

At the final refresh on 2026-09-13:

- official upstream `tiltedphoques/TiltedEvolution` `dev`:
  `8a3cec96c4955193df9a0f5e33e75b5364e63dd1`;
- this fork's integration branch:
  `d8f1344b728fe0b66d594f5d8705ca850b1a5254`;
- common ancestor: `9d81ef07d68e4bb2bd94fca246e798a564b7fb92`;
- divergence: 1,012 integration-only commits and 21 upstream-only commits;
- official tracker: 87 open issues and 12 open pull requests.

The branches are therefore not candidates for a wholesale merge. The quest
fork contains a large independent safety architecture, while upstream has new
wire, ownership, runtime and gameplay changes. Intake must be thematic and
reviewed one slice at a time.

Primary sources:

- [official repository](https://github.com/tiltedphoques/TiltedEvolution);
- [official issues](https://github.com/tiltedphoques/TiltedEvolution/issues);
- [official pull requests](https://github.com/tiltedphoques/TiltedEvolution/pulls).

### Reviewed mutable tips

These are evidence pins, not accepted implementation SHAs. The official
repository carries a [mixed GPL-3.0-or-later/LGPL-2.1 component
notice](https://github.com/tiltedphoques/TiltedEvolution/blob/dev/LICENSE);
license and attribution must be rechecked for each extracted file.

| Source | Reviewed head | State/disposition |
|---|---|---|
| PR #887 | `42a518a0581a90ef74e454900a22888af605241d` | Merged as `79df6a6d`; adapt and harden |
| PR #896 | `004119f1ce991ef9be954577c16e9f01606cf75e` | Merged as `8a3cec96`; adapt |
| PR #895 | `18a8c3d5df198331ab8fc0c68a574f1880955aba` | Open; later party lifecycle input |
| PR #894 | `ee9bba378c1624c445826b7a57dad3cc3ecca890` | Open; later duplicate-cast slice |
| PR #893 | `537e5099a07974cabaef34c1501a8b249e4b40ad` | Open; adapt fail-closed |
| PR #892 | `36afeebb7b5b88f19283d52e4dab9f1cbd65ac95` | Open; redesign, do not port as-is |
| PR #891 | `9b19246debc535949ce2a6fc48059c62c53cd466` | Open; adapt |
| PR #889 | `8a3cec96c4955193df9a0f5e33e75b5364e63dd1` | Open release PR; runtime comparison input |
| PR #879 | `bb65834b88de0b9dbc3affbe79b2ef2bd54db0b9` | Open; later presentation feature |
| PR #861 | `6bbc6a78d750b337f4dd617e07e7bd08ffe0fbf8` | Open; temporary degradation candidate |
| PR #854 | `a8c6472982c2f0df21bd8b50c71bae18855ad8ec` | Open; evidence only |
| PR #848 | `112493d63e5de34f1ada64eae5544ed6dcf4aabc` | Open; evidence only |
| PR #846 | `9f1d0cfdaf9d01226e8695af5ecbf38b36a5bc84` | Open; evidence only |
| PR #839 | `236fe5f09cebafb393980fcc1bc28b9c3e60a606` | Open; evidence only |

Related repository tips checked during the same refresh:

| Repository | Reviewed default-branch head | License signal |
|---|---|---|
| `rfortier/TiltedEvolution-rwf` | `a7d061564338ab7ff475d6919a19c42664589611` | Upstream-derived mixed license file; recheck per file |
| `cmpayc/TiltedEvolutionVR` | `29f99ede631d41009e49f083ec237ffad63bb216` | Upstream-derived mixed license file; recheck per file |
| `miredirex/skyrim-together-tweaks` | `276fabaf492f4f511a336305f1f558ac0754f812` | MIT detected |
| `Caelvanost/STRPluginMessagingAPI` | `62143fd463f305d9da6c5284b58d571e8671d7c5` | No license detected |
| `Caelvanost/TradeTogether` | `17199e4d82e2889919e6d9555e14c2144fd343f8` | No license detected |
| `Caelvanost/AnimSyncTogether` | `a432ec340f27eea0411e0a7c9eb048ad035c9d31` | MIT detected |
| `Caelvanost/IEDSyncTogether` | `ad4710a6b33b966af55d8a0326f1dae0e27460ed` | MIT detected |
| `TheLiberator78/Skyrim-Parity-Tool` | `81faa759f466c1ce1e2cca13b1603cea0ff582e4` | No license detected |
| `tiltedphoques/Mod-Compatibility` | `373a795e5b3b7d57ef08edb654e5c57ac186f60f` | No license detected |

## Priority A — port and harden before broad live gameplay

### Actor ownership epochs and former-owner rejection

Upstream merged [PR #887](https://github.com/tiltedphoques/TiltedEvolution/pull/887),
which replaces optimistic client ownership with versioned server grants. It
removes the timer-driven naked-NPC repair and carries ownership epochs through
inventory, equipment, actor values, death state, mounts and transfers. Follow-up
commits notify the previous owner even after it leaves range, fix pickpocket
broadcasting, clear ownership blacklists, and reject state updates from former
owners.

This directly addresses the same family as our observed ownership fights,
invalid entity routing, naked NPCs, disappearing equipment, instant inventory
repopulation and persistent actor corruption. The direction should be ported.

It is not accepted as-is because the merged PR chiefly has build, encoding and
manual play evidence rather than a dedicated state-machine test suite. Our
slice must additionally prove:

- only a current server grant can publish local authority;
- stale epochs are rejected for inventory, equipment, actor values and death;
- ownership loss revokes send authority before reassignment;
- disconnect/reconnect, cell exit/re-entry and entity-ID reuse cannot revive an
  old grant;
- duplicate and out-of-order transfer messages are idempotent;
- all item counts remain conserved;
- client and server version/capability mismatch is rejected before play.

The relevant upstream range is the merged ownership stack beginning with
`1f488f6f`, plus `adecbfcc`, `42a518a0` and `1660eb0c`. Preserve provenance even
if the implementation is rewritten around this fork's lifecycle fencing.

### Runtime/version support delta

Upstream commit `6f963497` adds Skyrim 1.7.99 and SKSE-related changes, while
`24dc4c4e` adds an incompatible-version message. These are useful comparison
sources for the fork's existing 1.6.1170 and 1.7.104 work.

Do not replace the current runtime resolver or copy numeric-version assumptions.
Compare exact executable, SKSE, Address Library database and hook identities;
then port only the independently verified differences. A friendly rejection
message is desirable, but it is presentation, not proof that a runtime is safe.

### Respawn and known quest patch evidence

Upstream includes a steady-clock camera recovery fix, interior respawn
overrides, a dragon spawn fix and a small Companions C02 plugin patch. These
should become separate gameplay slices after code review and exact-runtime live
tests. The C02 patch is also input to quest-profile generation: an ESP workaround
must be represented in the compatibility fingerprint and transition profile.

## Priority B — promising open pull requests requiring adaptation

### Object lifecycle and stale bindings — PR #891

[PR #891](https://github.com/tiltedphoques/TiltedEvolution/pull/891) keeps one
door/container binding per local reference, sends an explicit retirement
message, preserves the full server entity ID including generation bits, and
clears bindings on disconnect. It includes useful pure client/server regression
tests and both main CI platforms are green.

Adopt the identity and test approach, but add session generation, late-packet,
reconnect, duplicate retirement, ordered-delivery and shutdown coverage. The PR
explicitly does not provide persistent containers or full loaded-cell tracking,
and its extracted branch still lacks final in-game validation.

### Jail/evidence containers — PR #893

[Issue #700](https://github.com/tiltedphoques/TiltedEvolution/issues/700)
documents one player's inventory replacing another's after jail. [PR
#893](https://github.com/tiltedphoques/TiltedEvolution/pull/893) discovers the
personal prisoner/evidence containers through faction crime data instead of
adding more hard-coded chest IDs. That classification is much more scalable.

Adapt it with exact-runtime layout validation and fail-closed behavior. The
current PR says an unknown layout falls back to previous synchronization, which
can reproduce the destructive bug. In this fork, unavailable classification
must quarantine/exclude the custody transaction and produce a diagnostic reason.
Two-player arrest/release evidence is still required.

### Leveled NPC identity — PR #892

[Issue #639](https://github.com/tiltedphoques/TiltedEvolution/issues/639) shows
that local leveled-list RNG can produce different NPC base identities. [PR
#892](https://github.com/tiltedphoques/TiltedEvolution/pull/892) transports a
selected base identity and keeps it across assignment, spawning and ownership
transfer. The problem statement and reverse-engineered fields are valuable.

Do not port the implementation unchanged. Its current server accepts a
client-provided pick, some mismatch paths retain the local pick, and deferred
conformance is keyed mainly by local FormID. The rewritten slice must validate
the pick against the originating leveled list and mod mapping, bind deferred
work to full actor/server identity, session/generation and ownership epoch, and
quarantine rather than silently render divergent identities. Disable/re-enable
reconstruction also needs cell-transition, load, disconnect and shutdown tests.

### Dialogue initiated by a non-owner — PR #896

[PR #896](https://github.com/tiltedphoques/TiltedEvolution/pull/896) was merged
during this review. It distinguishes the current conversation speaker from
ambient speech, allowing the interacting player to publish dialogue even when
another client owns the NPC. This directly informs the observed missing or
stuck dialogue family.

Port the behavior only after binding it to the planned interaction session:
actor identity, interacting player, ownership epoch, dialogue/scene identity,
timeout and cancellation. Without that envelope, simultaneous conversations or
late speech can still race. Add tests for owner/non-owner initiators, overlapping
interactions, ownership transfer, scene end, disconnect and stale subtitles.

### Remote weapon, summon and party-invite fixes

- [PR #861](https://github.com/tiltedphoques/TiltedEvolution/pull/861) avoids a
  custom-enchantment path that makes remote NPC weapons disappear. It is a
  reasonable temporary degradation policy, but exact item-instance identity is
  the long-term fix.
- [PR #894](https://github.com/tiltedphoques/TiltedEvolution/pull/894) suppresses
  duplicate summon replication from staff enchantments on both send and receive
  paths. Reuse its duplicate-path test matrix after the ownership intake.
- [PR #895](https://github.com/tiltedphoques/TiltedEvolution/pull/895) replaces
  pointer-keyed party invitations with player IDs, expiry and single consumption.
  Its lifecycle rules are good input to networking/party hardening, but it is
  not on the quest P0 critical path.

## Quest and scene work — evidence, not a shortcut around P0

Open [PR #848](https://github.com/tiltedphoques/TiltedEvolution/pull/848)
contains an important finding: a scoped client override cannot suppress all
quest echo because Skyrim may execute the resulting quest event asynchronously
after the scope has ended. Its server-side short-lived dedup history and scene
experiments are useful test scenarios.

However, #848 is large, wire-incompatible with 1.8.0, merge-conflicting and
acknowledges unresolved scene/AI behavior. [PR
#839](https://github.com/tiltedphoques/TiltedEvolution/pull/839) suppresses
duplicate starts and [PR #846](https://github.com/tiltedphoques/TiltedEvolution/pull/846)
blocks a harmful member-originated QuestStop case, but both still operate inside
the old direct quest mutation model.

Use these as requirements for Tasks 11–14:

- asynchronous echo after an override scope ends;
- repeated start/stage/stop events;
- member-generated stop after leader success;
- legitimate repeatable stages and rewind behavior;
- scene master changes and dialogue without a stage transition;
- AI-package ownership during a scene.

Do not copy their direct `ScriptSetStage` path, timeout-only dedup, or force-stage
debug behavior into production. The server-authoritative revision, compatibility,
checkpoint, quiescence, verification and recovery gates remain mandatory.

[PR #854](https://github.com/tiltedphoques/TiltedEvolution/pull/854) is likewise
a useful scene/dialogue test catalogue, not an accepted solution. It is
merge-conflicting and explicitly leaves AI packages incomplete.

## Issue-derived product and regression requirements

The official tracker is evidence of recurrent bug families, not a specification.
The highest-value additions to our test matrix are:

- follower state and save persistence: [#792](https://github.com/tiltedphoques/TiltedEvolution/issues/792)
  reports unusable followers after disconnect, extreme duplicated inventory
  counts and persistent negative actor-value modifiers;
- downed/death recovery: [#645](https://github.com/tiltedphoques/TiltedEvolution/issues/645)
  reports essential NPCs remaining down for party members, and [#825](https://github.com/tiltedphoques/TiltedEvolution/issues/825)
  requests a co-op revive model;
- dialogue and interaction: [#118](https://github.com/tiltedphoques/TiltedEvolution/issues/118)
  asks that an NPC remain stable while being spoken to, matching the planned
  interaction lease rather than a permanent movement disable;
- actor identity and cell transitions: #639, object/actor visibility issues and
  [#845](https://github.com/tiltedphoques/TiltedEvolution/issues/845). The proposed
  leader-only door restriction is too broad; ownership and transition barriers
  should be fixed instead;
- personal inventory custody and quest items: #700 and quest-item pickup/transfer
  reports require explicit personal-versus-canonical storage classification;
- UI/input capture: menu, dialogue, controller and overlay reports require an
  explicit menu/input-session stack and a safe release watchdog.

Never use issue workarounds such as `recycleactor`, `removeallitems`, broad
`ResetInventory`, arbitrary actor-value correction or forced quest stages as
production self-healing. They can hide the causal fault and permanently mutate
the player's replica.

## Fork review

### rfortier/TiltedEvolution-rwf

The most active general fork reviewed was
[rfortier/TiltedEvolution-rwf](https://github.com/rfortier/TiltedEvolution-rwf).
At the snapshot its integration branch had 14 unique commits but was 24 commits
behind official `dev`. It contains the source branches for the scene/quest PRs
and an improved naked-NPC experiment.

Useful lessons from the naked-NPC work are to suppress inventory/equipment
publication while assignment is unresolved and never recreate inventory merely
to dress an actor. Its remaining timeout/equip fallback is not the authority
root fix and is superseded by upstream ownership epochs. The fork's open 1.5.97
PR spans a very broad set of files without a useful review description, so it
must not be merged as a compatibility shortcut.

### VR and other forks

The active [Skyrim Together VR fork](https://github.com/cmpayc/TiltedEvolutionVR)
demonstrates the value of an explicit runtime capability layer, but VR is outside
the current acceptance matrix. Other sampled forks were old, narrow deployment
variants or had no independently reviewed advantage over official `dev`.

## Add-ons and reusable design ideas

### Skyrim Together Tweaks

[Skyrim Together Tweaks](https://github.com/miredirex/skyrim-together-tweaks)
is an MIT-licensed SKSE companion plugin providing UI activation/key changes,
console access and interpolation/animation-delay tuning. An official collaborator
also recommends it for the F2/UI failure in [issue
#884](https://github.com/tiltedphoques/TiltedEvolution/issues/884).

Adopt the user-facing configuration ideas, preferably natively in this fork.
Do not make core correctness depend on its pattern scans, executable memory
patches or latest-AE-only plugin declaration. Any retained external compatibility
must be exact-build gated and fail closed when a pattern is absent.

### STR Plugin Messaging API and its consumers

[STR Plugin Messaging API](https://github.com/Caelvanost/STRPluginMessagingAPI)
demonstrates real demand for a stable add-on transport and a mapping from an
authenticated STR connection/player identity to a local remote-player FormID.
Its consumers include trading, animation, morph and equipment-display sync.

Valuable design rules are:

- expose stable identities, never cached raw `Actor*` pointers;
- copy bounded data in transport callbacks and schedule Skyrim access on the
  game thread;
- use named/versioned capabilities and ordered delivery;
- centralize version-specific discovery in one compatibility bridge;
- synchronize evaluated presentation results when reproducing an entire local
  configuration would be ambiguous.

Do not copy or depend on the current transport. It tunnels binary payloads
through chat, intercepts an internal receive path with a vectored exception
handler, targets one exact STR 1.8.0 binary, and the API repository had no
detected license at review time. The correct response is a first-party,
versioned extension channel with authentication, quotas, lifecycle invalidation
and a public proxy-identity API.

[TradeTogether](https://github.com/Caelvanost/TradeTogether) provides a useful
two-user offer/review/confirm UX and uses Skyrim's instance-aware transfer path,
but its repository also had no detected license and its documented flow does not
replace our need for server escrow, revisions, idempotency and conservation.

[AnimSyncTogether](https://github.com/Caelvanost/AnimSyncTogether) and
[IEDSyncTogether](https://github.com/Caelvanost/IEDSyncTogether) are MIT-licensed
sources of later-stage ideas: opt-in data-driven animation rules, avoidance of
double-driving vanilla behavior, stable plugin/local FormID identity, full-state
heartbeats and synchronization of final visual output. These belong after the
core authority platform and should remain presentation-only until explicitly
promoted.

The [Basic Co-op Bleedout System](https://www.nexusmods.com/skyrimspecialedition/mods/121113)
and the official experimental [respawn/resurrection commit](https://github.com/tiltedphoques/TiltedEvolution/commit/6ca2746f16aaa02e376738f2b08c90dc56b7db3e)
are useful UX prototypes. The Nexus implementation is two-player, spell/AOE
driven and documents teleport/quest limitations; neither replaces the planned
server-revisioned death state machine.

The experimental [Skyrim parity tool](https://github.com/TheLiberator78/Skyrim-Parity-Tool)
supports the product need for easier MO2 parity setup. Our safer direction is to
export signed/hash-based compatibility manifests and actionable differences
from the existing analyzer, without redistributing copyrighted mod assets.

The official [mod compatibility repository](https://github.com/tiltedphoques/Mod-Compatibility)
was last committed in 2022. It can supply historical test ideas but is not
current compatibility proof.

## Decisions

| Source | Decision | Required next proof |
|---|---|---|
| Upstream ownership epoch stack | Port and harden | State-machine tests, wire handshake, two-client inventory/NPC matrix |
| Upstream runtime changes | Compare and selectively port | Exact binary/SKSE/Address Library identities and live matrix |
| PR #891 object lifecycle | Adapt | Session/generation and late-packet tests, final live validation |
| PR #893 jail containers | Adapt fail-closed | Runtime layout gate and two-player arrest/release |
| PR #892 leveled NPCs | Redesign using research | Server validation and P0-D-quality deferred identity envelope |
| PR #896 dialogue | Adapt into interaction sessions | Concurrent/stale dialogue and ownership-transfer tests |
| PRs #839/#846/#848/#854 | Test/design evidence only | Tasks 11–14 authority and recovery gates |
| Skyrim Together Tweaks | Reuse UX ideas; do not depend for correctness | Native implementation or exact-build compatibility gate |
| STRPM/TradeTogether | Design evidence only | First-party transport/API; license and security review |
| AnimSync/IEDSync | Later opt-in presentation work | Stable extension API and cross-mod two-client tests |
| `recycleactor`/`ResetInventory`/forced stages | Reject | Root-cause repair and quarantine instead |

## Safe intake order

1. Accept Tasks 07 and 08 without mixing unrelated gameplay/network changes.
2. Reconcile the merged upstream runtime delta in a narrow compatibility slice.
3. Port the merged ownership epoch stack plus follow-ups, with dedicated tests.
   This replaces the separate plan to keep a naked-NPC self-healing loop.
4. Re-slice observational diagnostics on that authority baseline.
5. Evaluate #891 and #893 as separate object/inventory safety slices.
6. Use #892 and #896 to design actor identity and interaction-session slices;
   do not stack their current implementations blindly.
7. Feed #839/#846/#848/#854 scenarios into Tasks 11–14 while preserving disabled
   canonical mutation until the complete P0 proof exists.
8. Add a first-party extension API only after the core protocol/capability and
   lifecycle model is stable.

Every external candidate must record its exact source SHA, license, affected
wire/ABI/authority boundaries, missing evidence and local replacement tests.
Open PR tips are mutable; pin the reviewed commit before implementation.

## Monitoring rule

Refresh official `dev`, issues and open PRs before every new gameplay or
networking slice and before a release candidate. Re-run this broader ecosystem
review at least at each milestone boundary. New external code never becomes an
automatic dependency merely because it appears to fix the same visible symptom.
