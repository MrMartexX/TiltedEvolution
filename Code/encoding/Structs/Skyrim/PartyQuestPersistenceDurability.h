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
 *
 * P0-H now has narrow OS-level stable-storage primitives for file flushes and,
 * on Linux, directory fsync. Those primitives are evidence only until every
 * authoritative metadata/data publication path adopts the required ordering.
 * The Windows parent-directory publication barrier also remains deliberately
 * unsupported rather than inferred from undocumented or administrator-only
 * mechanisms. Therefore their existence does not upgrade CurrentLocalGuarantee.
 *
 * Terminology rule: legacy identifiers/comments that use "durable" describe
 * persisted transaction/recovery ordering only within CurrentLocalGuarantee
 * unless they explicitly require PowerLossDurable. Such naming is not evidence
 * that bytes reached stable storage across sudden power loss.
 */
struct PartyQuestPersistenceDurabilityPolicy
{
    static constexpr PartyQuestPersistenceGuarantee CurrentLocalGuarantee =
        PartyQuestPersistenceGuarantee::ProcessCrashResilient;
    static constexpr PartyQuestPersistenceGuarantee MinimumPoCRuntimeMutationGuarantee =
        PartyQuestPersistenceGuarantee::ProcessCrashResilient;
    static constexpr PartyQuestPersistenceGuarantee MinimumProductionRuntimeMutationGuarantee =
        PartyQuestPersistenceGuarantee::PowerLossDurable;

    [[nodiscard]] static constexpr bool Meets(
        PartyQuestPersistenceGuarantee aProvided,
        PartyQuestPersistenceGuarantee aRequired) noexcept
    {
        return static_cast<uint8_t>(aProvided) >= static_cast<uint8_t>(aRequired);
    }

    [[nodiscard]] static constexpr bool IsProductionRuntimeMutationReady(
        PartyQuestPersistenceGuarantee aProvided) noexcept
    {
        return Meets(aProvided, MinimumProductionRuntimeMutationGuarantee);
    }

    [[nodiscard]] static constexpr bool AllowsNativeRuntimeMutation() noexcept
    {
        return IsProductionRuntimeMutationReady(CurrentLocalGuarantee);
    }
};
