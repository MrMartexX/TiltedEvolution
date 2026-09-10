# Skyrim/SKSE async save contract — Task 07

## Scope and exact runtime identity

This audit targets the isolated Steam runtime `SkyrimSE.exe 1.6.1170.0` at
`D:\Games\Skyrim Special Edition 1.6.1170`.

- `SkyrimSE.exe` SHA-256: `C434208894F07F604B852F29B8EDC3A58C4DE63DE783373733E72B2B73F33BE9`
- installed `skse64_1_6_1170.dll` file version: `0.2.2.6`
- installed SKSE DLL SHA-256: `C9A2C8A80DF6BF2372C5F49468BB2E5AB67786157265B6F29ECE9F4EAC075D54`
- Address Library file: `versionlib-1-6-1170-0.bin`
- public request entry: `BGSSaveLoadManager::Save_Impl`, Address Library IDs `34818 / 35727`
- CommonLibSSE-NG evidence checkout: `b93280e832f263dbef44e44cbe2936622a02f91a`
- locally available SKSE source checkout: `4cd2e34face74d5247fa38888c7542ad45d4a1d2`

The available SKSE checkout is newer than installed SKSE 2.2.6. Its save hook
is useful corroborating source evidence, but is not treated as exact binary
identity proof for 2.2.6.

## Proven source-level pipeline

1. CommonLib's `BGSSaveLoadManager::Save()` merely calls
   `Save_Impl(2, 0, fileName)`.
2. `BGSSaveLoadManager` owns a dedicated `Thread` containing an eight-entry
   `BSTCommonStaticMessageQueue<BSTSmartPointer<bgs::saveload::Request>, 8>`.
   Therefore `Save_Impl` return is request admission, not write completion.
3. The SKSE save hook runs in the deeper save operation. It obtains `saveName`
   from the worker-owned operation object (`unk0 + 0xBB0` in the available
   source), calls `Serialization::SetSaveName(saveName)`, runs the original
   save target, and only then clears the name.
4. `Serialization::SetSaveName` calls `MakeSavePath`. `MakeSavePath` reads
   `sLocalSavePath:General` at that later point and stores the resulting `.skse`
   path in process-global `s_savePath`.
5. `SkyrimVM::SaveGlobalData_Hook` calls the original VM save and then
   `Serialization::HandleSaveGlobalData`. That routine deletes the previous
   co-save, creates the `.skse`, invokes plugin serialization callbacks, writes
   headers and closes `s_currentFile`.

Consequences:

- `sLocalSavePath` is not proven to be captured into the request before
  `Save_Impl` returns. SKSE demonstrably reads it later.
- Restoring a temporary global path immediately after `Save_Impl` returns can
  route the delayed `.skse` operation to the ordinary save directory.
- Holding the override until an unproven delay is also unsafe: a queued manual
  or autosave could inherit the co-op path.
- File existence, stable size, successful `Save_Impl`, or the SKSE
  `kMessage_SaveGame` message is not completion evidence.

## Missing authoritative signals (exact blocker)

No currently proven public API supplies all of the following for 1.6.1170 and
SKSE 2.2.6:

1. an immutable per-request directory used by both Skyrim and SKSE;
2. an exact request ID carried from enqueue through both writers;
3. a successful close/replace notification for the matching `.ess`;
4. a successful close notification for the matching `.skse`;
5. propagation of SKSE plugin callback/write/close failure to that request;
6. cancellation and shutdown retirement under the save worker's ownership.

In the available SKSE source, a plugin serialization exception is caught and
logged, after which serialization continues; `HandleSaveGlobalData` returns
`void`. A created co-save therefore cannot be treated as successful solely
because control returned or a file exists.

The Bethesda request layout and completion event payloads exposed by
CommonLibSSE-NG remain partly unknown. `BSSaveDataEvent` is forward-declared and
the `bgs::saveload::Request` layout/completion contract is not defined. Hooking
an assumed worker return or unknown event would be speculative ABI work.

## Required contract before enabling engine PreRepair capture

`PartyQuestAsyncSaveContract` defines the minimum correlation boundary. A live
observer must produce two authoritative `ClosedSuccess` observations bound to
the exact campaign, player profile, runtime generation, transaction, world
revision, capture epoch, attempt nonce and save name. Either order is accepted;
duplicates are idempotent. Any mismatch, write failure, timeout, cancellation,
generation transition or shutdown fails closed and requires confined cleanup.

The completion capability is move-only and is issued once, only after both
artifacts close successfully. It performs no I/O and grants no save or mutation
authority.

## Decision

`PartyQuestSkyrimEngineSaveIsolationPolicy::AllowsProductionCapture()` remains
`false`. This is intentional. A source- and live-evidence-backed injection and
completion seam is still required before changing it. Canonical SetStage also
remains disabled.

## Future read-only live proof

Before any enabling patch, exact-binary instrumentation must record monotonic
timestamps and the full request identity for enqueue, worker start, path
capture, `.ess` close/replace, SKSE path capture, `.skse` close, and terminal
success/failure. The test must use a disposable isolated root and prove that a
concurrent manual/autosave remains in its original directory. LoadGame,
MainMenu, shutdown, disk-full and forced co-save failure must all terminate the
request without issuing completion.
