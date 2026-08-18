#pragma once

#include <cstdint>
#include <filesystem>

enum class PartyQuestStableStorageStatus : uint8_t
{
    Success,
    Unsupported,
    InvalidPath,
    OpenFailed,
    NodeValidationFailed,
    FlushFailed,
    CloseFailed
};

/**
 * Narrow OS-level stable-storage primitives for P0-H work.
 *
 * These calls provide evidence about an individual flush operation only. They
 * do not authorize a filesystem path, do not make a multi-file transaction
 * atomic and do not by themselves upgrade PartyQuestPersistenceDurabilityPolicy.
 * Callers must retain their existing path confinement, ancestor/topology
 * validation, verification and recovery ordering.
 *
 * FlushFile rejects a symlink/reparse final node and non-regular-file nodes
 * rather than intentionally following them. This is only final-node hardening;
 * it is not a replacement for the caller's confined namespace proof.
 */
struct PartyQuestStableStorage
{
    [[nodiscard]] static PartyQuestStableStorageStatus FlushFile(
        const std::filesystem::path& acPath) noexcept;

    [[nodiscard]] static PartyQuestStableStorageStatus FlushDirectory(
        const std::filesystem::path& acDirectory) noexcept;

    [[nodiscard]] static PartyQuestStableStorageStatus FlushParentDirectory(
        const std::filesystem::path& acPath) noexcept;

    [[nodiscard]] static constexpr bool HasDocumentedFileFlushPrimitive() noexcept
    {
        return true;
    }

    /**
     * Linux exposes fsync() on directory file descriptors for persisting
     * directory-entry changes. The current Windows implementation deliberately
     * reports Unsupported: P0-H must not infer a directory durability contract
     * from undocumented or administrator-only mechanisms.
     */
    [[nodiscard]] static constexpr bool HasDocumentedParentDirectoryFlushPrimitive() noexcept
    {
#ifdef _WIN32
        return false;
#else
        return true;
#endif
    }
};
