#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

/** Immutable local bounds for decoded durable metadata and pre-allocation reads. */
struct PartyQuestDurableResourcePolicy
{
    static constexpr uint64_t MaxIdentityArchiveBytes = 1024;
    static constexpr uint64_t MaxCanonicalStateArchiveBytes = 64ull * 1024ull * 1024ull;
    static constexpr uint64_t MaxReplicaMetadataArchiveBytes = 1ull * 1024ull * 1024ull;
    static constexpr uint64_t MaxRuntimeApplyArchiveBytes = 16ull * 1024ull * 1024ull;
    static constexpr uint32_t MaxSerializedPathBytes = 1024;
    static constexpr size_t MaxFilesystemPathBytes = MaxSerializedPathBytes;
    // Reserve room for every sibling name used by crash-safe durable publication,
    // including the campaign identity's longest `.bak.tmp` path.
    static constexpr size_t MaxCrashSafePathSuffixBytes = 64;
    static constexpr size_t MaxMutableFilesystemPathBytes =
        MaxFilesystemPathBytes - MaxCrashSafePathSuffixBytes;
    static_assert(MaxFilesystemPathBytes > MaxCrashSafePathSuffixBytes);
    static constexpr uint64_t MaxCommittedRuntimeRecords = 65536;
    static constexpr uint64_t MaxCanonicalJournalRecords = 65536;
    static constexpr uint64_t MaxRevisionCheckpointsPerKind = 128;
    // Count and bytes are independent local authorities. Without the byte cap,
    // 128 individually valid maximum-sized replica publications could retain an
    // impractically large checkpoint namespace. The scan bound keeps admission
    // itself finite even when the on-disk namespace has been externally altered.
    static constexpr uint64_t MaxRevisionCheckpointRetainedBytesPerKind =
        8ull * 1024ull * 1024ull * 1024ull;
    static constexpr size_t MaxRevisionCheckpointInspectedEntriesPerKind = 100000;

    [[nodiscard]] static bool CanRetainRevisionCheckpointBytes(
        uint64_t aRetainedBytes,
        uint64_t aPlannedBytes) noexcept
    {
        return aRetainedBytes <= MaxRevisionCheckpointRetainedBytesPerKind &&
            aPlannedBytes <=
                MaxRevisionCheckpointRetainedBytesPerKind - aRetainedBytes;
    }

    [[nodiscard]] static bool IsFilesystemPathWithinBudget(
        const std::filesystem::path& acPath) noexcept
    {
        return HasBoundedFilesystemPathBytes(acPath, MaxFilesystemPathBytes);
    }

    [[nodiscard]] static bool IsMutableFilesystemPathWithinBudget(
        const std::filesystem::path& acPath) noexcept
    {
        return HasBoundedFilesystemPathBytes(acPath, MaxMutableFilesystemPathBytes);
    }

private:
    [[nodiscard]] static bool HasBoundedFilesystemPathBytes(
        const std::filesystem::path& acPath,
        size_t aMaxBytes) noexcept
    {
        try
        {
            const auto value = acPath.lexically_normal().generic_u8string();
            return !value.empty() && value.size() <= aMaxBytes;
        }
        catch (...)
        {
            return false;
        }
    }
};
