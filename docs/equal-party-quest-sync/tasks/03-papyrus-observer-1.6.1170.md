# Task 03 — Support a proven Papyrus/quiescence observer on Skyrim 1.6.1170

Read `../MASTER-HANDOFF.md`. Start from the accepted integration HEAD after Tasks 01–02. Follow the master protocol and do only this task.

## Goal

Provide a read-only, exact-runtime Papyrus observer for the supported Skyrim `1.6.1170` test runtime so the runtime monitor can prove quiescence or return a bounded fail-closed status. The current adapter explicitly authorizes only `1.7.104`.

## Required research before code

1. Identify the exact `SkyrimSE.exe` binary in the controlled 1.6.1170 environment by version plus stable executable identity; account for both real 1.6.1170 Address Library variants.
2. Use primary code/evidence: matching SKSE/CommonLibSSE-NG sources, vtable/function definitions, the exact binary and Address Library data. Do not extrapolate 1.7.104 offsets.
3. Determine what each sampled Papyrus field means, its synchronization domain and whether reading it concurrently is legal. Pointer readability is not semantic proof.
4. Prove hook/vtable slot signatures and executable targets before extending the supported-profile table.

## Implementation requirements

- Represent runtime support as an explicit immutable profile selected by exact runtime/binary evidence.
- Validate every required address, object, vtable entry, field range and generation before publication.
- The observer remains read-only. It must never pause/drive Papyrus or manufacture an idle result.
- Return distinct states for Busy, Idle, Unsupported, Unavailable, InvalidEvidence, GenerationChanged and Timeout where existing APIs permit.
- Sampling must be bounded, cancellation-aware and tied to the process owner/generation. A lifecycle change between samples invalidates the result.
- No exceptions across native ABI; unknown state rejects mutation.
- Preserve 1.7.104 support only if its existing proof remains valid.

## Regression and validation tests

Cover exact-profile selection; both 1.6.1170 binary variants or explicit rejection of an unproven variant; wrong hash/version; missing Address Library; invalid/non-executable vtable; null/unreadable VM; changing generation; Busy→Idle; permanently Busy timeout; inconsistent double sample; uninstall/shutdown; concurrent lifecycle invalidation; exception containment.

Add a controlled live diagnostic on 1.6.1170 that records observer profile, runtime identity, bounded samples and final state without enabling mutation. Verify ordinary play, save/load and scene transitions do not hang or crash.

## Done when

- The exact installed 1.6.1170 runtime either has a source-reviewed, live-confirmed observer or is rejected with a precise external evidence blocker.
- Idle is never inferred from unavailable/unsupported data.
- Observer results are generation-bound and usable by existing quiescence primitives.
- Canonical mutation remains disabled; all four CI jobs are FULL GREEN with exact counts, plus recorded live evidence for the supported profile.
