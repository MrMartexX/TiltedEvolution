#pragma once

#include <cstddef>
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
    WriteFailed,
    FlushFailed,
    CloseFailed,
    RenameFailed,
    RemoveFailed,
    CreateDirectoryFailed
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
 * File operations reject a symlink/reparse final node and non-regular-file
 * nodes rather than intentionally following them. This is only final-node
 * hardening; it is not a replacement for the caller's confined namespace proof.
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
     * Establishes a directory tree as stable namespace evidence.
     *
     * POSIX walks every path component from an existing absolute root, rejects
     * symlink/non-directory components, creates missing directories one at a
     * time, and fsyncs both the containing directory and the resulting directory
     * at each step. Existing components are also parent-fsync'd so a directory
     * created by an earlier crash-resilient path can be promoted later.
     *
     * Windows validates every traversed component as a non-reparse directory.
     * Missing descendants are created one at a time and the affected parent and
     * child directories are flushed with NtFlushBuffersFileEx Flags=0 on NTFS.
     * An already-existing final directory is promoted by flushing its parent and
     * itself. Unsupported filesystems, unavailable native flush support or
     * insufficient directory write access fail closed instead of being relabeled
     * PowerLossDurable.
     */
    [[nodiscard]] static PartyQuestStableStorageStatus EnsureDirectoryTreeDurably(
        const std::filesystem::path& acDirectory) noexcept;

    /**
     * Creates/truncates and durably writes one regular staged file.
     *
     * POSIX writes through an O_NOFOLLOW descriptor, fsyncs the file, closes it,
     * then fsyncs the containing directory so a newly created name is durable.
     * Windows first validates an existing non-reparse NTFS parent, then creates
     * the file with FILE_FLAG_WRITE_THROUGH, writes all bytes, calls
     * FlushFileBuffers and closes the exact handle. Non-NTFS Windows paths fail
     * closed before the target file is created or truncated.
     */
    [[nodiscard]] static PartyQuestStableStorageStatus WriteFileDurably(
        const std::filesystem::path& acPath,
        const void* apData,
        size_t aSize) noexcept;

    /**
     * Streams one exact regular file into a new/truncated regular destination and
     * establishes both destination data and its directory entry as stable.
     *
     * POSIX uses O_NOFOLLOW handles for both source and destination, validates the
     * source as a regular file, copies with bounded memory, fsyncs the destination,
     * closes both descriptors, then fsyncs the destination parent.
     *
     * Windows requires an already-existing reviewed NTFS destination namespace,
     * pins the source against concurrent write/delete, rejects reparse/non-regular
     * and hard-linked source/destination nodes, compares exact file identity before
     * truncation, streams through exclusive FILE_FLAG_WRITE_THROUGH destination
     * authority, flushes that handle, then crosses the NTFS parent-directory
     * metadata barrier. The primitive never creates a missing parent directory.
     */
    [[nodiscard]] static PartyQuestStableStorageStatus CopyFileDurably(
        const std::filesystem::path& acSource,
        const std::filesystem::path& acDestination) noexcept;

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
     */
    [[nodiscard]] static PartyQuestStableStorageStatus PublishFileRename(
        const std::filesystem::path& acSource,
        const std::filesystem::path& acDestination,
        bool aReplaceExisting = false) noexcept;

    /**
     * Durably removes one regular file namespace entry.
     *
     * POSIX performs unlink followed by fsync(parent). Windows requires an exact
     * non-reparse, single-link regular file on a reviewed NTFS namespace, marks
     * it through FileDispositionInfo using DELETE access, closes the exact
     * handle, verifies that the name is actually absent (so delayed deletion is
     * never mislabeled success), then crosses the NTFS parent-directory metadata
     * barrier. This is single-entry authority only and never follows aliases.
     */
    [[nodiscard]] static PartyQuestStableStorageStatus RemoveFileDurably(
        const std::filesystem::path& acPath) noexcept;

    /**
     * Durably removes one already-empty directory namespace entry.
     *
     * POSIX validates the final node as a real directory, calls rmdir(), then
     * fsyncs the containing directory. Windows opens the final node without
     * following reparse points, requires NTFS, marks the exact handle for
     * deletion, then durably promotes the containing directory.
     */
    [[nodiscard]] static PartyQuestStableStorageStatus RemoveEmptyDirectoryDurably(
        const std::filesystem::path& acDirectory) noexcept;

    [[nodiscard]] static constexpr bool HasDocumentedFileFlushPrimitive() noexcept
    {
        return true;
    }

    [[nodiscard]] static constexpr bool HasDocumentedDurableFileWritePrimitive() noexcept
    {
        return true;
    }

    [[nodiscard]] static constexpr bool HasDocumentedDurableFileCopyPrimitive() noexcept
    {
        return true;
    }

    /** Implementation exists on both build platforms; Windows is runtime-gated to NTFS + NtFlushBuffersFileEx. */
    [[nodiscard]] static constexpr bool HasDocumentedDurableDirectoryTreePrimitive() noexcept
    {
        return true;
    }

    /**
     * Linux exposes fsync() on directory file descriptors for persisting
     * directory-entry changes. Generic Windows FlushDirectory intentionally
     * remains Unsupported: Windows stable-storage operations use the narrower,
     * runtime-gated NTFS NtFlushBuffersFileEx contract through
     * EnsureDirectoryTreeDurably instead of granting a generic directory-flush
     * capability to arbitrary callers.
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
        return true;
    }

    [[nodiscard]] static constexpr bool HasDocumentedDurableEmptyDirectoryRemovalPrimitive() noexcept
    {
        return true;
    }
};
