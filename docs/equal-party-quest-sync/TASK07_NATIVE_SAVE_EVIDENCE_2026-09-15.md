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

## Exact-runtime provider gate added after the inspection

Research commit `c1c853d9d5d4ee1afdc704d34e936b918beb9cd3`
adds a non-authorizing capability query. It verifies the reviewed 1.6.1170
save-utility vtable slot and byte sequences for initial path preparation,
utility save, native write and close, the late path call, and both rename call
sites. Any mismatch returns false before isolated-save admission. It installs
no new hook and leaves `kRequestWideIsolationProven` false.

The verified BSWin32 save-utility vtable contains 18 slots. Its path method is
slot 2 (`0x15302A0`); the Save_Impl branch invokes slot 7 (`0x152FD60`). Slots
7 and 12 contain closely related write-and-event paths, while slots 5, 8 and 9
perform other file/event operations. This broader map prevents treating one
observed writer as the entire utility contract.

- Full MSVC v143 x64 Release build passed.
- Five native exact-byte matcher checks passed, including mismatch/null/empty
  rejection and unavailable-runtime rejection.
- Four hook safety regression checks passed.
- Resulting research DLL SHA256:
  `41CFEAC614EB1B9A3D8D6BAED4612F53D9F70F24260E24C5596C4D640534CB19`.
- The DLL remains outside the game installation. The research branch is local
  and was not pushed to the upstream SKSE repository.

Task 7 still lacks a completed-save trace and the actual request-owned provider.
P0 NOT CLOSED.
