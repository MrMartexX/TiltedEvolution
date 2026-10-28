# Task 04 — Build a deterministic offline compatibility analyzer

Read `../MASTER-HANDOFF.md`. Start from the accepted integration HEAD after Task 03. Follow the master protocol and work only on analysis/artifact generation; do not authorize quests yet.

## Goal

Create a reproducible tool that extracts the evidence needed to review quest transitions without repeatedly hashing all plugins and scripts on Skyrim's update thread. It produces review input, not runtime authorization.

## Inputs and outputs

Inputs must be explicit: exact Skyrim/runtime identity, ordered plugin set and load order, winning overrides, BSA/loose script sources, relevant quest records/fragments/scripts, and analyzer version.

Output should be a deterministic, reviewable manifest candidate containing at least:

- quest `GameId` and source plugin identity;
- runtime and mod-environment fingerprint;
- resolved record/topology fingerprint;
- winning-override fingerprint;
- script/Papyrus dependency fingerprint;
- known stages/objectives/fragments/aliases/scenes and side-effect indicators;
- transition edges with reasons for SafeCandidate, Unsupported or NeedsReview;
- analyzer schema/version and complete provenance.

Do not silently omit unreadable records or archives. An incomplete input set must yield an explicit invalid/incomplete artifact.

## Required work

1. Reuse the exact hash/domain definitions used by `PartyQuestSkyrimRuntimeCompatibilityEvidence`; eliminate duplicate algorithms or add cross-tests proving equality.
2. Parse only from stable snapshots/copies, with deterministic ordering, normalized paths/encodings and fixed integer widths.
3. Distinguish plugin bytes, winning override topology and script content. A plugin timestamp or filename is not sufficient identity.
4. Detect loose-file versus BSA override precedence and record all relevant sources.
5. Produce machine-readable output plus a concise human review report. The generated candidate must not enter `kReviewedProfiles` automatically.
6. Keep runtime hot path to immutable cached lookup. No per-server-update filesystem tree scan.

## Tests

Use small fixtures for load-order changes; ESL/standard FormID mapping; winning override change; BSA versus loose script; missing/corrupt archive; same filenames with changed bytes; path/case normalization; stable repeat output; analyzer-version change; Linux/Windows determinism; runtime hash parity with `CaptureEnvironmentSnapshot`/`ComputeEnvironmentFingerprints`; cancellation and partial-output cleanup.

Include a performance test or measured budget showing no full environment scan occurs during a quest update/game-thread consume path.

## Done when

- The tool generates byte-stable review artifacts from identical inputs on supported platforms.
- Runtime fingerprint code and offline analyzer agree on the same environment.
- Incomplete/ambiguous data is visibly rejected.
- No profile is automatically authorized and mutation stays disabled.
- All required CI is FULL GREEN with exact counts; generated sample artifacts and invocation are documented.
