#pragma once

#include <Structs/Skyrim/PartyQuestPlayerProfile.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

enum class PartyQuestPlayerProfilePersistenceStatus : uint8_t
{
    Success,
    FileNotFound,
    IoError,
    InvalidMagic,
    UnsupportedVersion,
    Truncated,
    ChecksumMismatch,
    InvalidData,
    PowerLossDurabilityUnsupported
};

struct PartyQuestPlayerProfilePersistenceResult
{
    PartyQuestPlayerProfilePersistenceStatus Status{PartyQuestPlayerProfilePersistenceStatus::InvalidData};
    std::optional<PartyQuestPlayerProfileId> ProfileId;
    bool UsedBackup{};
};

enum class PartyQuestPlayerProfilePersistenceBoundary : uint8_t
{
    TemporaryVerified,
    PrimaryMovedToBackup,
    TemporaryPublished
};

enum class PartyQuestPlayerProfilePersistenceDirective : uint8_t
{
    Continue,
    FailClosed
};

/** Ephemeral local fault observer; it grants no path or storage authority. */
struct PartyQuestPlayerProfilePersistenceHooks
{
    using Callback = PartyQuestPlayerProfilePersistenceDirective (*)(
        PartyQuestPlayerProfilePersistenceBoundary,
        void*) noexcept;

    Callback OnBoundary{};
    void* Context{};

    [[nodiscard]] PartyQuestPlayerProfilePersistenceDirective Invoke(
        PartyQuestPlayerProfilePersistenceBoundary aBoundary) const noexcept
    {
        return OnBoundary
            ? OnBoundary(aBoundary, Context)
            : PartyQuestPlayerProfilePersistenceDirective::Continue;
    }
};

/**
 * Persists the stable identity of one local co-op character/profile.
 *
 * The profile id is immutable metadata. Unlike the runtime side-effect journal,
 * falling back to its previous identical identity backup is safe.
 *
 * GenerateProfileId() creates an identity value only. It does not prove that the
 * value belongs to the currently loaded Skyrim character/save lineage and must
 * never be used to manufacture PartyQuestPlayerProfileLineageAuthorization.
 * The live Skyrim path requires independent filename-free lineage evidence.
 *
 * SavePowerLossDurably is the stronger metadata publication path. Its parent
 * directory must already exist and already satisfy the caller's namespace
 * durability contract; this method only proves the archive file/rename sequence.
 */
class PartyQuestPlayerProfilePersistence final
{
public:
    [[nodiscard]] static PartyQuestPlayerProfileId GenerateProfileId() noexcept;

    [[nodiscard]] static std::vector<uint8_t> Encode(
        const PartyQuestPlayerProfileId& acProfileId);

    [[nodiscard]] static PartyQuestPlayerProfilePersistenceResult Decode(
        const std::vector<uint8_t>& acBytes);

    [[nodiscard]] static PartyQuestPlayerProfilePersistenceStatus SaveAtomically(
        const std::filesystem::path& acPath,
        const PartyQuestPlayerProfileId& acProfileId);

    [[nodiscard]] static PartyQuestPlayerProfilePersistenceStatus SavePowerLossDurably(
        const std::filesystem::path& acPath,
        const PartyQuestPlayerProfileId& acProfileId,
        PartyQuestPlayerProfilePersistenceHooks aHooks = {});

    [[nodiscard]] static PartyQuestPlayerProfilePersistenceResult Load(
        const std::filesystem::path& acPath);
};
