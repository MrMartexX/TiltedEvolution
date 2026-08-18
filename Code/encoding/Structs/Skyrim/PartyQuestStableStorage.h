#pragma once

#include <cstdint>
#include <filesystem>

enum class PartyQuestStableStorageStatus : uint8_t
{
    Success,
    Unsupported,
    InvalidPath,
    CrossDirectoryRename,
    OpenFailed,
    NodeValidationFailed,
    FlushFailed,
    CloseFailed,
    RenameFailed
};

/**
 * Narrow OS-level stable-storage primitives for P0-H work.
 *
 * These calls provide evidence about an individual flush/publication operation
 * only. They do not authorize a filesystem path, do not make a multi-file
 * transaction atomic and do not by themselves upgrade
 * PartyQuestPersistenceDurabilityPolicy. Callers must retain their existing
 * path confinement, ancestor/topology validation, verification and recovery
 * ordering.
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

    /**
     * Publishes one already-complete regular file by a same-directory rename.
     *
     * On POSIX the source file is fsync'd before rename and the containing
     * directory is fsync'd after rename. If the rename succeeds but the final
     * directory flush fails, the result is still failure/uncertain publication;
     * callers must preserve recovery authority rather than rolling state forward.
     * Cross-directory moves are rejected because they require a different
     * durability proof for both namespaces.
     *
     * Windows deliberately reports Unsupported until a documented non-admin
     * publication protocol is accepted for P0-H. This method is not path
     * authorization and must only be used after the existing confinement proof.
     */
    [[nodiscard]] static PartyQuestStableStorageStatus PublishFileRename(
        const std::filesystem::path& acSource,
        const std::filesystem::path& acDestination) noexcept;

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

    [[nodiscard]] static constexpr bool HasDocumentedAtomicFilePublicationPrimitive() noexcept
    {
#ifdef _WIN32
        return false;
#else
        return true;
#endif
    }
};
