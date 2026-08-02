#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeSafety.h>

#include <cstddef>
#include <cstdint>
#include <unordered_map>

enum class PartyQuestRuntimeCompatibilityStatus : uint8_t
{
    Authorized,
    UnknownQuest,
    InvalidRequirement,
    InvalidClientFacts,
    ProfileVersionMismatch,
    ResolvedRecordMismatch,
    WinningOverrideMismatch,
    ScriptMismatch,
    NativeAdapterMismatch
};

/**
 * Exact compatibility requirements for one quest-specific native repair adapter.
 * Fingerprints are opaque deterministic hashes produced by the future manifest
 * builder; zero is reserved for missing/unknown evidence and is never accepted.
 */
struct PartyQuestRuntimeCompatibilityRequirement
{
    GameId QuestId{};
    uint32_t ProfileVersion{};
    uint64_t ResolvedRecordFingerprint{};
    uint64_t WinningOverrideFingerprint{};
    uint64_t ScriptFingerprint{};
    uint64_t NativeAdapterFingerprint{};

    bool operator==(const PartyQuestRuntimeCompatibilityRequirement&) const noexcept = default;
};

/** Runtime evidence supplied by the local client before adapter authorization. */
struct PartyQuestRuntimeCompatibilityFacts
{
    uint32_t ProfileVersion{};
    uint64_t ResolvedRecordFingerprint{};
    uint64_t WinningOverrideFingerprint{};
    uint64_t ScriptFingerprint{};
    uint64_t NativeAdapterFingerprint{};

    bool operator==(const PartyQuestRuntimeCompatibilityFacts&) const noexcept = default;
};

struct PartyQuestRuntimeCompatibilityDecision
{
    PartyQuestRuntimeCompatibilityStatus Status{PartyQuestRuntimeCompatibilityStatus::UnknownQuest};
    PartyQuestRuntimeSafetyProfile SafetyProfile;

    [[nodiscard]] bool IsAuthorized() const noexcept
    {
        return Status == PartyQuestRuntimeCompatibilityStatus::Authorized &&
            SafetyProfile.HasVerifiedNativeAdapter();
    }
};

/**
 * Issues the otherwise-unforgeable RuntimeSafe profile only after exact
 * requirement/fact matching. Unknown or partially described compatibility is
 * fail-closed.
 */
class PartyQuestRuntimeCompatibilityPolicy final
{
public:
    [[nodiscard]] static bool IsValidRequirement(
        const PartyQuestRuntimeCompatibilityRequirement& acRequirement) noexcept;

    [[nodiscard]] static PartyQuestRuntimeCompatibilityDecision Evaluate(
        const PartyQuestRuntimeCompatibilityRequirement& acRequirement,
        const PartyQuestRuntimeCompatibilityFacts& acFacts) noexcept;
};

/**
 * Campaign compatibility profile for quest-specific native adapters.
 * Duplicate quest requirements are rejected instead of silently replacing an
 * already reviewed compatibility contract.
 */
class PartyQuestRuntimeCompatibilityManifest final
{
public:
    bool AddRequirement(const PartyQuestRuntimeCompatibilityRequirement& acRequirement);

    [[nodiscard]] const PartyQuestRuntimeCompatibilityRequirement* FindRequirement(
        const GameId& acQuestId) const noexcept;

    [[nodiscard]] PartyQuestRuntimeCompatibilityDecision Evaluate(
        const GameId& acQuestId,
        const PartyQuestRuntimeCompatibilityFacts& acFacts) const noexcept;

    [[nodiscard]] size_t GetRequirementCount() const noexcept { return m_requirements.size(); }

private:
    std::unordered_map<GameId, PartyQuestRuntimeCompatibilityRequirement> m_requirements;
};
