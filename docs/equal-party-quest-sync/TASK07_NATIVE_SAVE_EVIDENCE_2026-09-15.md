# Task 7: native save-path and completion evidence, 2026-09-15

Status: loaded-code inspection; not a completed-save or fault-injection test.
Canonical mutation and engine PreRepair capture remain disabled.

## Provenance

The isolated 1.6.1170 MO2 instance launched its existing configured fork. A
read-only `ReadProcessMemory` utility captured bounded function windows; it did
not suspend the process, install a hook, call a save, or change a save path.

- SkyrimSE.exe SHA256:
  `C434208894F07F604B852F29B8EDC3A58C4DE63DE783373733E72B2B73F33BE9`.
- Installed SkyrimTogether.exe file SHA256:
  `BACF9E7BCFAA22034DB1CF4286BC9E88A3A44BA93DBE5A71C62E3D49E8EF7343`.
  This is the previously installed artifact, **not** an artifact built from PR #9.
- Snapshot UTC: `2026-09-15T10:00:19.7124204Z`; process 31960;
  main image base `0x140000000`.
- Local snapshot:
  `D:\Codex\Artifacts\task07-save-targets\snapshot-20260915-100019-7355392.json`.
- Snapshot SHA256:
  `96E34A6504E3CC5DB1597EC50C6B7913C1249C95A0020D0CD003AAE0A70C48F8`.
- Source used to interpret declarations: CommonLibSSE-NG
  `b93280e832f263dbef44e44cbe2936622a02f91a` and the inspected SKSE 2.2.6 source.
- Local tooling: `D:\Codex\Workspaces\2026-09-14\task07-skse-provider`;
  Python parser tests: 11 passed; Windows read-only memory helper: four checks
  passed (readback, invalid address, size bound, disposed handle). A non-Skyrim
  target was also rejected. Disassembly used Capstone 5.0.9 and PE import names
  from pefile 2024.8.26. These are tool tests, not TPTests or a runtime save proof.

The live save-path vtable slot matched its reviewed disk target. Other captured
functions include existing installed hooks: `Save_Impl` starts with a detour,
and its SKSE call site is patched. Interpret original fall-through code and
hook call ordering separately; this snapshot does not prove every hook's
behavior, full loaded-image identity, or a production ABI.

## Address Library discrimination for these targets only

| AE ID | `versionlib-1-6-1170-0.bin` RVA | `versionlib-1-6-1170-0-1.bin` RVA |
|---:|---:|---:|
| 255912, BSWin32SaveDataSystemUtility vtable | `0x1AC8CB8` | `0x1AC5688` |
| 255939, BSSaveDataSystemUtilityFile vtable | `0x1AC9128` | `0x1AC5AF8` |

For the exact executable hash above, the first database's two targets pass
MSVC RTTI class/locator and executable-slot checks. The second database fails
the expected locator check. Both supplied together are rejected as ambiguous
by the diagnostic utility. No Address Library file was renamed or replaced.
This does not establish a universal database preference or validate all IDs.

## Control flow observed in the loaded code

1. `Save_Impl`, AE ID 35727 / RVA `0x60FD40`, calls `0x60F8E0` at
   `0x60FF0E`, before the patched SKSE save-hook call at `0x60FFF3`.
2. `0x60F8E0` invokes the utility's virtual slot 2 at `0x60F94A`, supplying
   a 0x104-byte path buffer. The verified slot points to `PrepareFileSavePath`
   at `0x15302A0`. It passes this resulting path to `0x152E2E0` at `0x60FA4F`.
3. `PrepareFileSavePath` reads path pointers while executing (`0x15302D8`,
   `0x15302F1`) and builds the output with imported string functions. A late
   setting override inside the SKSE hook cannot retroactively change the path
   already selected by this earlier call.
4. After the SKSE hook returns, `Save_Impl` has a branch that constructs an
   object whose disk RTTI is `bgs::saveload::SaveOperationRequest`, stores the
   save-buffer pointer at `+0x148`, and submits it at `0x6102E7`. Another
   branch calls the utility's virtual slot 7 at `0x610359`.
5. Slot 7 resolves to `0x152FD60`. It uses the buffer path at `+0x64`, opens a
   file through `0xCFB3B0`, writes through `0xCFBC40`, calls the destructor at
   `0x152FE1C`, then constructs and dispatches the save-data event at
   `0x152FE35` / `0x152FE44`.
6. The manager's save-data event handler, RVA `0x6138A0`, calls
   `PrepareFileSavePath` again at `0x6139A8` and performs backup/final rename
   operations at `0x613AB4` and `0x613ACE`.

This is a code-order finding, not an event trace collected during a save.
It identifies **multiple path-resolution phases** and shows why the old
temporary `sLocalSavePath` assignment around `SaveGame_HookTarget` cannot be
accepted as request-wide isolation.

## Why the native event is insufficient as a success certificate

- `0xCFBC40` calls imported `WriteFile` at `0xCFBC6F`, reports an API failure
  as a nonzero result, and exposes bytes-written through an output pointer.
  The inspected slot-7 caller uses the result but does not compare that byte
  count with the requested size before constructing its event status.
- `0xCFB400` calls `0xCFBFA0`. The latter calls imported `CloseHandle` at
  `0xCFBFB3`; the destructor/event path does not propagate a checked close
  result. No explicit flush is present in this inspected write/close chain.
- The manager's observed `rename` calls are followed by further instructions
  without checking their return value. Event receipt alone therefore does
  not certify successful final-name publication, much less power-loss durability.

These observations do not assert that disk-full, short-write, or rename failure
actually happened in this session. They establish missing proof in promoting
the native event to the project's stronger completion capability.

## Required implementation boundary

The next provider must route every matching path-resolution phase from an
owned immutable request, including co-save path construction and final rename.
It must retain that request until file closure and final publication are
checked, including the queued branch. A scoped global-setting swap and the
existing worker-return flag are not suitable implementations.

Before enabling it, demonstrate checked create/write/byte count/flush/close/
rename results, failure propagation, cancellation and request retirement;
then execute overlapping-save and fault tests on an isolated disposable root.
The event structure's observed offsets are research findings, not permission
to install a speculative event sink or change the production bridge ABI.

## Research-provider safety correction

Local SKSE research commit `6faa561ca4f71d6f3ef88d6ec5f1700d9ba90c7f`
on `codex/task07-checked-cosave` rejects isolated-save admission before reading
request fields or publishing pending state. The hook no longer temporarily
changes `sLocalSavePath` or retires a request at hook return. It retains the
ordinary SKSE save sequence and reports zero in the legacy `workerReturned`
field: the native ESS completion outcome is still unknown. The separately
checked co-save outcome is not promoted to ESS success.

- Full MSVC v143 x64 Release SKSE DLL build succeeded, Windows SDK 10.0.26100.0.
- Four structural regression tests passed: admission remains disabled before
  request access; no hook-time global path swap/early retirement; ordinary
  save-call ordering remains once; the event does not certify ESS completion.
  These are source-shape tests, not dynamic engine-save tests.
- DLL SHA256:
  `A3F30BD9AE7D94FD99DE3BD075F396894C3DA4A6A34DA40C0CC54D581A27643E`.
- Output stays in `D:\Codex\Artifacts\task07-checked-cosave`; **not installed**.
  No research commit was pushed to the upstream SKSE repository.
- The STR production capture policy and native mutation policy remain disabled.
  STR TPTests were not rerun for this documentation update; the SKSE build is
  not a substitute for the four STR CI checks or live save validation.

Task 7 still lacks a completed-save trace and the actual request-owned provider.
P0 NOT CLOSED.

## Offline exact-binary follow-up, 2026-09-18

The exact `SkyrimSE.exe` above was inspected without launching Skyrim. The
SteamStub 3.1 header was decoded with the reviewed launcher algorithm and its
AES fields were used to decrypt a temporary in-memory copy of `.text`; the game
file was not modified. Header signature `0xC0DEC0DF`, original entry point
`0x153BC64`, code RVA `0x1000`, code size `0x174DA00`, and zero DRM flags were
recovered from the exact binary.

This closes the previously missing normal queued-control-flow proof for that
binary:

1. `Save_Impl` constructs a 0x150-byte `SaveOperationRequest`, gives it the
   operation discriminator `0x40000001`, stores the save-buffer pointer at
   `+0x148`, and submits it at `0x6102E7`.
2. The queue worker at `0x617820` selects discriminator `0x40000001` and, at
   `0x617905`, loads that exact `+0x148` buffer.
3. The worker calls the save utility's slot 7 at `0x617962` and only after that
   call returns invokes the request's virtual destructor at `0x617985`.
4. Address Library AE ID 206598 resolves the request vtable to `0x188CFE0` for
   the accepted `versionlib-1-6-1170-0.bin`; its first slot is the scalar
   destructor at `0x61B740`. The destructor clears its retained operation state,
   runs the base cleanup and conditionally frees the 0x150-byte request.

Local research commits on `codex/task07-checked-cosave` now bind the immutable
reservation to the exact buffer returned by `CreateSaveBuffer`, retire normal
work only after checked slot-7 return, and use the identity-bound request
destructor as the no-writer/discard drain boundary. A destructor belonging to
an ordinary or different save buffer cannot retire the reserved request. The
enqueue hook additionally binds the exact `SaveOperationRequest` address, so a
later allocation that reuses only the buffer address cannot pass retirement
validation. If slot 7 returns before any checked write/close/rename hook (for
example, because file creation failed), the provider emits a terminal ESS
failure before request retirement instead of leaving the portable contract
pending. Hook installation reserves the complete 66 trampoline bytes and verifies
every patched pointer/call target before publishing provider readiness.

Latest local native commit at the time of this note: `e5dc7eb`. Full MSVC v143
x64 Release build succeeded; 17 structural safety tests and the native
state/target executable passed. DLL SHA256:
`D54718A4B8CFB38FD60172882E059F3B2BC7AA338FBDE9526C88AD0A6F04088F`.

This remains research evidence, not production authorization. The provider is
still compile-time disabled. Fault-injection, overlapping-save, authenticated
bridge/lifecycle ownership, isolated live `.ess`/`.skse` validation and the
Task 8 durability proof remain required. P0 NOT CLOSED.

## Portable authorization and routing follow-up, 2026-09-19

Local branch `codex/task07-save-event-adapter` now contains one fail-closed
portable route from decoded native events to finalization. PQS3 always flows
through adapter, logical contract and finalization gate; PQS4 is decoded and
routed directly to the gate, which is the sole owner allowed to retire the
logical contract. The former adapter API that independently retired the
contract was removed.

The route also requires a current move-only provider-registration token bound
to the exact registration instance and runtime generation. The fixed provider
descriptor requires event ABI v2, the complete PQS3/PQS4 and checked-I/O
capability set, the exact implementation fingerprint and the currently proven
Skyrim 1.6.1170 runtime tuple. Descriptor compatibility is explicitly not
module/source authentication; `RegisterAuthenticated` may only be called by a
future trusted loader after it establishes the module and export identity.
Invalidation revokes authority immediately, and an old token cannot regain it
after same-generation re-registration.

Latest portable commit at the time of this note: `bd12080d`. The focused MSVC
test executable passed 1131 assertions in 44 test cases. No Skyrim process was
launched and no provider was installed or enabled.

Still required before production wiring: trusted module/export resolution,
validated callback registration and lifetime, serialized callback handoff
under the existing generation fence, and P0-C-owned unregister/quiescence on
shutdown. A descriptor or structurally valid payload alone grants no source
authority. Task 7 and Task 8 remain open; P0 NOT CLOSED.

### Native descriptor export follow-up

Native research commit `2d94456` adds the fixed 64-byte
`PartyQuestSKSE_GetSaveProviderDescriptor` export with the same ABI version,
implementation version, capability mask, 1.6.1170 runtime tuple and provider
fingerprint required by the portable policy. The export is fail-closed: it
zeroes the caller's correctly sized output and returns false unless the native
provider has already published verified readiness. An invalid pointer/size or
the compile-time-disabled provider cannot produce an approved descriptor.

The full MSVC v143 x64 Release DLL build, all 17 structural tests and the
native descriptor/state executable passed. `dumpbin /exports` confirmed both
`PartyQuestSKSE_GetSaveProviderDescriptor` and the legacy readiness export in
the built DLL. DLL SHA256:
`A2718C5CFCE9F0BE77D47F075BD2EFB911700F608F847E81349810B495FC4D61`.

The DLL was not loaded into a non-Skyrim process, installed or executed.
Descriptor presence is compatibility evidence only. Trusted resolution of the
already loaded module, callback registration ownership and unload/quiescence
remain external blockers; the provider remains disabled.
