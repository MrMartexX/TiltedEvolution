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
    RenameFailed,
    RemoveFailed
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
     * On POSIX the exact source file is fsync'd before rename and the containing
     * directory is fsync'd after rename. On Windows the source is opened with
     * FILE_FLAG_WRITE_THROUGH, the filesystem is required to identify as NTFS,
     * the exact handle is flushed, and FileRenameInfo is issued through that
     * handle. Microsoft documents that write-through requests cause NTFS to
     * flush metadata changes including rename operations.
     *
     * If rename succeeds but a later flush/close reports failure, publication is
     * uncertain and the result remains failure. Callers must preserve recovery
     * authority rather than rolling logical state forward. Cross-directory moves
     * are rejected because they require a different durability proof.
     *
     * aReplaceExisting is intended for durable primary -> backup rotation. It
     * does not weaken same-directory or regular-source validation.
     *
     * This method is not path authorization and must only be used after the
     * existing confinement proof.
     */
    [[nodiscard]] static PartyQuestStableStorageStatus PublishFileRename(
        const std::filesystem::path& acSource,
        const std::filesystem::path& acDestination,
        bool aReplaceExisting = false) noexcept;

    /**
     * Durably removes one regular file namespace entry when the platform has a
     * reviewed primitive. POSIX performs unlink followed by fsync(parent).
     * Windows deliberately remains Unsupported until delete-on-close / metadata
     * publication semantics are accepted with the same confidence as NTFS rename.
     */
    [[nodiscard]] static PartyQuestStableStorageStatus RemoveFileDurably(
        const std::filesystem::path& acPath) noexcept;

    [[nodiscard]] static constexpr bool HasDocumentedFileFlushPrimitive() noexcept
    {
        return true;
    }

    /**
     * Linux exposes fsync() on directory file descriptors for persisting
     * directory-entry changes. The current Windows implementation deliberately
     * reports Unsupported for a generic directory flush; NTFS rename durability
     * is instead provided by the narrower write-through publication primitive.
     */
    [[nodiscard]] static constexpr bool HasDocumentedParentDirectoryFlushPrimitive() noexcept
    {
#ifdef _WIN32
        return false;
#else
        return true;
#endif
    }

    /**
     * A reviewed atomic publication implementation exists on both build
     * platforms. Windows support is runtime-gated to NTFS; unsupported filesystems
     * still fail closed from PublishFileRename().
     */
    [[nodiscard]] static constexpr bool HasDocumentedAtomicFilePublicationPrimitive() noexcept
    {
        return true;
    }

    [[nodiscard]] static constexpr bool HasDocumentedDurableFileRemovalPrimitive() noexcept
    {
#ifdef _WIN32
        return false;
#else
        return true;
#endif
    }
};
