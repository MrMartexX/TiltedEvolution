#pragma once

#include <cstdint>

enum class PartyQuestPersistenceGuarantee : uint8_t
{
    Volatile,
    ProcessCrashResilient,
    PowerLossDurable
};

/**
 * Explicit durability decision for the current PoC.
 *
 * The existing complete-temp + stream-flush + rename/backup persistence is
 * accepted as process-crash resilience for PoC runtime mutation barriers. It
 * does not issue OS/file/device and parent-directory durable flushes, so it
 * must never be advertised as power-loss durable.
 */
struct PartyQuestPersistenceDurabilityPolicy
{
    static constexpr PartyQuestPersistenceGuarantee CurrentLocalGuarantee =
        PartyQuestPersistenceGuarantee::ProcessCrashResilient;
    static constexpr PartyQuestPersistenceGuarantee MinimumPoCRuntimeMutationGuarantee =
        PartyQuestPersistenceGuarantee::ProcessCrashResilient;

    [[nodiscard]] static constexpr bool Meets(
        PartyQuestPersistenceGuarantee aProvided,
        PartyQuestPersistenceGuarantee aRequired) noexcept
    {
        return static_cast<uint8_t>(aProvided) >= static_cast<uint8_t>(aRequired);
    }
};
