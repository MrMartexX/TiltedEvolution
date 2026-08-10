#pragma once

#include <cstddef>
#include <cstdint>

/** Immutable local bounds for decoded durable metadata and pre-allocation reads. */
struct PartyQuestDurableResourcePolicy
{
    static constexpr uint64_t MaxIdentityArchiveBytes = 1024;
    static constexpr uint64_t MaxCanonicalStateArchiveBytes = 64ull * 1024ull * 1024ull;
    static constexpr uint64_t MaxReplicaMetadataArchiveBytes = 1ull * 1024ull * 1024ull;
    static constexpr uint64_t MaxRuntimeApplyArchiveBytes = 16ull * 1024ull * 1024ull;
    static constexpr uint32_t MaxSerializedPathBytes = 1024;
    static constexpr uint64_t MaxCommittedRuntimeRecords = 65536;
    static constexpr uint64_t MaxCanonicalJournalRecords = 65536;
    static constexpr uint64_t MaxRevisionCheckpointsPerKind = 128;
};
