# Party Quest offline compatibility analyzer

`PartyQuestCompatibilityAnalyzer` creates deterministic evidence for a human
compatibility review. It does not edit the reviewed-profile registry, authorize
a quest, or enable runtime mutation.

Analyze a stable copy of a Skyrim `Data` directory. Every active plugin must be
listed in load order. Mark ESL/light plugins as `lite`. List every relevant BSA
with increasing resource priority and every loose PEX explicitly; loose PEX
always wins over an identically named archived script.

```text
PartyQuestCompatibilityAnalyzer \
  --data D:/snapshots/skyrim-1.6.1170/Data \
  --runtime 1.6.1170.0 \
  --runtime-sha256 <64 lowercase or uppercase hex characters> \
  --analyzer-version 1 \
  --plugin standard:Skyrim.esm \
  --plugin standard:Update.esm \
  --plugin lite:Example.esl \
  --archive 10:Skyrim_-_Scripts.bsa \
  --loose-pex Scripts/ExampleQuest.pex \
  --quest Skyrim.esm:3372B \
  --manifest out/candidate.json \
  --report out/review.txt
```

The command refuses to overwrite either output. It publishes the JSON/text
pair atomically and removes temporary output after failure or cancellation.
Exit code `0` means a complete review artifact, `1` means an incomplete but
explicitly published fail-closed artifact, `2` means invalid command syntax,
and `3` means cancellation.

The manifest separately records the complete plugin/script environment,
resolved quest topology, winning override, script dependencies, stages,
objectives, aliases, scenes, possible side effects, and transition review
reasons. Identical stable inputs generate identical bytes on Windows and Linux.
Changing plugin order, plugin bytes, script bytes, analyzer version, quest set,
or source priority changes the appropriate fingerprint.

The runtime path performs only immutable cached fingerprint lookups. The full
filesystem pass belongs to snapshot capture/offline analysis and is never run
for each server quest update.

The files in `docs/samples/party-quest-offline-analyzer` are byte-for-byte
generated complete and fail-closed/incomplete examples from the test fixtures.
