#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeApply.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

enum class PartyQuestRuntimeApplyPersistenceStatus : uint8_t
{
    Success,
    FileNotFound,
    IoError,
    PowerLossDurabilityUnsupported,
    InvalidMagic,
    UnsupportedVersion,
    Truncated,
    ChecksumMismatch,
    InvalidData,

    /**
     * Only an older .bak archive is trustworthy. It must not be accepted as a
     * normal current journal because doing so could forget a newer mutation
     * barrier or committed transaction and repeat Skyrim side effects.
     */
    BackupRecoveryRequired,
    ResourceLimitExceeded
};

struct PartyQuestRuntimeApplyPersistenceResult
{
    PartyQuestRuntimeApplyPersistenceStatus Status{PartyQuestRuntimeApplyPersistenceStatus::InvalidData};
    std::optional<PartyQuestRuntimeRecoveryState> State;
    bool UsedBackup{};
    bool UsedTemporary{};
};

enum class PartyQuestRuntimeApplyPersistenceBoundary : uint8_t
{
    TemporaryVerified,
    PrimaryMovedToBackup,
    TemporaryPublished
};

enum class PartyQuestRuntimeApplyPersistenceDirective : uint8_t
{
    Continue,
    FailClosed
};

/** Local-only fault observer; it carries no paths, bytes or filesystem authority. */
struct PartyQuestRuntimeApplyPersistenceHooks
{
    using Callback = PartyQuestRuntimeApplyPersistenceDirective (*)(
        PartyQuestRuntimeApplyPersistenceBoundary,
        void*) noexcept;

    Callback OnBoundary{};
    void* Context{};

    [[nodiscard]] PartyQuestRuntimeApplyPersistenceDirective Invoke(
        PartyQuestRuntimeApplyPersistenceBoundary aBoundary) const noexcept
    {
        return OnBoundary
            ? OnBoundary(aBoundary, Context)
            : PartyQuestRuntimeApplyPersistenceDirective::Continue;
    }
};

/**
 * Durable sidecar for client runtime-apply idempotency and crash recovery.
 *
 * It stores committed transaction fingerprints plus an optional in-progress
 * recovery marker. It does not store Skyrim save data or execute repairs.
 * A stale backup is never silently promoted to current runtime truth.
 */
class PartyQuestRuntimeApplyPersistence final
{
public:
    [[nodiscard]] static std::vector<uint8_t> Encode(
        const PartyQuestRuntimeRecoveryState& acState);

    [[nodiscard]] static PartyQuestRuntimeApplyPersistenceResult Decode(
        const std::vector<uint8_t>& acBytes);

    [[nodiscard]] static PartyQuestRuntimeApplyPersistenceStatus SaveAtomically(
        const std::filesystem::path& acPath,
        const PartyQuestRuntimeRecoveryState& acState,
        PartyQuestRuntimeApplyPersistenceHooks aHooks = {});

    /**
     * Stronger P0-H publication path for this one journal only.
     *
     * The destination directory must already exist; this method deliberately
     * does not claim durable ancestor-directory creation. It writes and verifies
     * the .tmp archive, durably rotates an existing primary to .bak with
     * replace-existing semantics, then durably publishes .tmp as primary.
     * A failure after primary -> backup preserves backup/temporary recovery
     * authority and does not perform an unproven rollback rename.
     *
     * This method proving one writer does not upgrade the global durability
     * policy. Unsupported filesystem semantics fail closed with
     * PowerLossDurabilityUnsupported.
     */
    [[nodiscard]] static PartyQuestRuntimeApplyPersistenceStatus SavePowerLossDurably(
        const std::filesystem::path& acPath,
        const PartyQuestRuntimeRecoveryState& acState,
        PartyQuestRuntimeApplyPersistenceHooks aHooks = {});

    [[nodiscard]] static PartyQuestRuntimeApplyPersistenceResult Load(
        const std::filesystem::path& acPath);
};
