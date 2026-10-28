# Task 12 — Create the first reviewed production quest profiles

Read `../MASTER-HANDOFF.md`. Start after Tasks 04, 05 and 11 are accepted. Follow the master protocol. Adding a profile is authorization-sensitive; do not enable native mutation in this task.

## Goal

Populate `PartyQuestSkyrimRuntimeCompatibilityEvidence::BuildReviewedManifest()` with a deliberately small set of exact, evidence-backed transition profiles suitable for controlled live validation. Do not attempt to support hundreds of quests at once.

## Selection rules

Choose the simplest representative vanilla transitions after analyzer output and source/runtime review. Prefer transitions that:

- are forward, single-edge and repeatably reachable;
- have no player alias, scene, created reference, inventory/quest-object or generic world mutation;
- have bounded/understood Papyrus fragments;
- expose clear observable postconditions;
- can be tested with disposable saves on two clients;
- do not gate large irreversible world changes.

Reject a famous/convenient quest if its side effects are not narrow. Do not assume the Whiterun quest used during diagnostics is safe.

## Required evidence per profile

- exact quest `GameId`, plugin/load mapping and supported Skyrim binary/runtime;
- analyzer version and source artifact hashes;
- resolved topology, winning override, plugin environment and script fingerprints;
- exact source→target edge and required objectives/state;
- fragment/script/alias/scene/reference audit;
- required readiness and Papyrus quiescence rules;
- complete preconditions/postconditions;
- recovery behavior and reason the transition is safe enough for controlled testing;
- reviewer-visible report committed beside or generated from the profile source.

## Required work

1. Produce analyzer artifacts from the controlled 1.6.1170 MO2 environment.
2. Manually review records, winning overrides and every executed fragment/script on each selected edge.
3. Encode immutable reviewed requirements through the validator from Task 05. Generated data alone must not self-authorize.
4. Make manifest publication all-or-nothing: one invalid/duplicate/conflicting profile invalidates the candidate registry.
5. Ensure different load order, modified script/plugin or runtime variant fails closed without a costly game-thread rescan.
6. Document exactly what is unsupported; profile count is a safety property, not a success metric.

## Tests

For every accepted profile: exact match; source/target boundary; pre/postconditions; duplicate/replay; changed plugin/script/load order; wrong runtime/binary; same quest ID from another mapping; missing environment cache; unsupported adjacent edge; forbidden side-effect evidence; malformed duplicate profile; deterministic manifest fingerprint.

## Done when

- At least one useful transition has complete committed evidence and passes runtime admission in dry-run.
- No unreviewed quest/edge can borrow authorization from a reviewed one.
- Live dry-run on both intended client environments reports the expected profile without a freeze.
- `SetStage` remains disabled and all required CI is FULL GREEN with exact counts.
