#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeApply.h>

#include <cstdint>

enum class PartyQuestRuntimeDryRunOutcome : uint8_t
{
    WouldApply,      // All gates passed; mutation safely simulated without execution
    Stale,           // Generation changed, revision out of order, or candidate superseded
    Unsupported,     // Compatibility mismatch, forbidden surfaces, unreviewed transition
    RetryableBusy,   // Reference not loaded, Papyrus VM busy / non-quiescent, session busy
    Timeout,         // Quiescence wait or readiness budget timed out
    RecoveryBlocked, // Runtime crash recovery pending or checkpoint restore required
    Invalid          // Malformed request, missing authority, or schema violation
};

struct PartyQuestRuntimeDryRunResult
{
    PartyQuestRuntimeDryRunOutcome Outcome{PartyQuestRuntimeDryRunOutcome::Invalid};
    GameId QuestId{};
    uint64_t TransactionId{};
    uint64_t TargetWorldRevision{};
    uint64_t RuntimeGeneration{};

    [[nodiscard]] bool WouldApply() const noexcept { return Outcome == PartyQuestRuntimeDryRunOutcome::WouldApply; }
};
