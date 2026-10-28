# Task 07 request-owned save lifecycle and publication contract

Status: local implementation; stacked on the Task 07 review branch. This does
not enable engine capture or close Task 07, Task 08, or P0.

## Purpose and missing obligation

`PartyQuestAsyncSaveContract` already owns the immutable correlation identity
and rejects overlapping requests. Its original completion rule, however,
treated two checked-close observations as sufficient and exposed an
unconditional `Retire()`. Loaded-code evidence for Skyrim 1.6.1170 shows that
the `.ess` final rename occurs after the inspected write/close path and that
the rename result is not checked. A local logical terminal therefore cannot
prove either final-name publication or that the external save queue has
stopped writing.

The contract now separates four facts:

1. logical request ownership;
2. checked write/flush/close for each exact artifact;
3. checked publication under each exact final name;
4. physical retirement of the request from all native I/O.

No filesystem observation, delay, worker-return bit, engine terminal status,
or native event receipt substitutes for these facts.

## Immutable identity and ownership

`Begin` copies and retains the existing campaign ID, player profile ID,
runtime generation, transaction ID, target world revision, capture epoch,
attempt nonce, and canonical save name. The save name is derived from the
transaction, revision, and attempt, so the contract adds no redundant identity
field. The copied identity cannot change during the request.

Only one request owns the save pipeline. Every later observation and retirement
must match that complete identity. A different attempt, generation, player, or
other identity is stale and cannot mutate the active request. Rejected `Begin`
and stale observations leave all close and publication evidence unchanged.

## Artifact phases

For each of `SkyrimEss` and `SkseCosave`, the provider reports:

- `Observe(..., ClosedSuccess, ...)` only after all required create, write,
  full byte-count, seek/truncate where applicable, flush, and close results for
  that artifact are checked successfully;
- `Observe(..., Failed, ...)` when any required pre-publication operation fails
  or an authoritative result is unavailable;
- `ObservePublication(..., PublishedSuccess, ...)` only after the exact final
  name is checked to be published successfully;
- `ObservePublication(..., Failed, ...)` when final rename/replace/direct-name
  publication fails or its authoritative result is unavailable.

Publication success before checked close is a protocol failure. Close success
alone remains pending, even after both artifacts close. Completion is issued
exactly once and only after both artifacts have checked close and checked final
publication success. The move-only completion remains correlation proof only;
it grants no persistence, capture, or mutation authority.

For a direct-to-final-name writer, publication is still a distinct provider
fact: it means the checked successful close made the complete artifact visible
at the identity-bound final path. For a temporary-file writer, it means the
subsequent checked rename/replace succeeded. A provider must not report both
facts from one unchecked event merely because no rename is expected.

Duplicate close or publication success for the same phase is idempotent.
Either phase may arrive first across the two different artifacts, but phases
for one artifact remain ordered: close before publication. An unknown artifact
or outcome discriminant, an outcome conflict, clock rollback, timeout, or an
allocation exception fails closed and never issues a completion. Once a
logical terminal is reached, later I/O outcomes cannot revive it.

## Cancellation, timeout, and physical retirement

Cancellation and timeout are logical terminals. They suppress completion but
retain the request identity and exclusive reservation because native work may
still be queued or writing. Failure and successful completion retain the same
reservation for the same reason. A new `Begin` deterministically returns
`Busy` until retirement.

`Retire(identity)` is the separate physical-I/O acknowledgement. The native
provider may call it only after it has authoritatively established that the
matching request has left every queue/callback/writer and can no longer create,
write, close, rename, replace, or delete either artifact. A pending request
must first receive failure or cancellation; retirement cannot silently discard
pending work. A stale retirement cannot release a newer attempt. Cleanup is
required for failed/cancelled/timed-out attempts, but `CleanupRequired` is not
permission to touch their files before physical retirement.

If admission to the native queue fails after `Begin`, the owner must first
cancel or fail the matching request, then retire it after proving that no
native work was admitted. If shutdown cannot prove drain, ownership remains
reserved and the higher-level runtime must stay fail-closed rather than admit a
replacement request.

## Serialization and exception boundary

This pure helper owns no threads and is not internally thread-safe. The future
provider/consumer must serialize `Begin`, observations, polling, cancellation,
and retirement under one external owner or lock. Validation and state mutation
must occur inside that same serialization boundary. No raw game pointer may be
stored in or used as request identity.

All calls are `noexcept`. Identity or completion allocation failure leaves no
success token. C++ exceptions must still be contained before crossing any
Skyrim, SKSE, or plugin ABI boundary.

## Native provider boundary still required

The provider must supply versioned, exact-runtime-gated observations carrying
the full immutable identity. It must own request-specific path resolution for
every Skyrim and SKSE phase and produce checked results for both writers and
both final names. It must also supply the authoritative queue/writer-drained
fact used to justify `Retire(identity)`.

This pure contract does not implement that provider, inspect files, route
paths, install hooks, launch Skyrim, or change the production capture policy.
Until exact-runtime overlap and fault-injection evidence validates the native
provider, production engine capture and canonical mutation remain disabled.

P0 NOT CLOSED.
