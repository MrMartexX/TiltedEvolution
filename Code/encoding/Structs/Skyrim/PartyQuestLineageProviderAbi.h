#pragma once

#include <cstdint>

enum class PartyQuestLineageBridgeEvidenceState : uint32_t
{
    Unavailable = 0,
    CandidateUnpersisted = 1,
    Persisted = 2,
    Invalid = 3
};

enum class PartyQuestLineageProviderKind : uint32_t
{
    Unknown = 0,
    SkseCosave = 1,
    EmbeddedClient = 2
};

enum class PartyQuestLineageProviderCapability : uint64_t
{
    StableSequencedSnapshot = 1ull << 0u,
    PersistedCosaveRecord = 1ull << 1u,
    FilenameIndependent = 1ull << 2u,
    PreLoadInvalidation = 1ull << 3u,
    NewGameCandidate = 1ull << 4u
};

inline constexpr uint32_t kPartyQuestLineageProviderAbiVersion = 1u;
inline constexpr uint64_t kPartyQuestRequiredLineageProviderCapabilities =
    static_cast<uint64_t>(
        PartyQuestLineageProviderCapability::StableSequencedSnapshot) |
    static_cast<uint64_t>(
        PartyQuestLineageProviderCapability::PersistedCosaveRecord) |
    static_cast<uint64_t>(
        PartyQuestLineageProviderCapability::FilenameIndependent) |
    static_cast<uint64_t>(
        PartyQuestLineageProviderCapability::PreLoadInvalidation) |
    static_cast<uint64_t>(
        PartyQuestLineageProviderCapability::NewGameCandidate);

// Deterministic contract identity, not an authentication secret.
inline constexpr uint64_t kPartyQuestSkseCosaveProviderFingerprintV2 =
    0x32564F434C515450ull;

struct PartyQuestLineageRuntimeVersion final
{
    uint32_t Major{};
    uint32_t Minor{};
    uint32_t Patch{};
    uint32_t Build{};

    [[nodiscard]] constexpr bool operator==(
        const PartyQuestLineageRuntimeVersion&) const noexcept = default;
};

/** Fixed ABI returned by PartyQuestLineageProvider_GetDescriptor. */
struct PartyQuestLineageProviderDescriptor final
{
    uint32_t AbiVersion{};
    uint32_t StructSize{};
    uint32_t ProviderKind{};
    uint32_t Reserved0{};
    uint64_t Capabilities{};
    uint32_t RuntimeMajor{};
    uint32_t RuntimeMinor{};
    uint32_t RuntimePatch{};
    uint32_t RuntimeBuild{};
    uint64_t ProviderFingerprint{};
    uint64_t Reserved1{};
    uint64_t Reserved2{};
};

/** Fixed ABI returned by PartyQuestLineageBridge_GetSnapshot. */
struct PartyQuestLineageBridgeSnapshot final
{
    uint32_t AbiVersion{};
    uint32_t StructSize{};
    uint64_t Sequence{};
    uint32_t State{};
    uint32_t Reserved{};
    uint64_t ProfileHigh{};
    uint64_t ProfileLow{};
};

static_assert(sizeof(PartyQuestLineageProviderDescriptor) == 64u);
static_assert(sizeof(PartyQuestLineageBridgeSnapshot) == 40u);

/**
 * Central registry of intended compatibility targets.
 *
 * Listing a runtime here grants no live authority. The exact provider ABI,
 * fingerprint, capability set and runtime tuple must still match, and the
 * surrounding client must independently load its exact VersionDb profile.
 */
struct PartyQuestLineageTargetRuntimeRegistry final
{
    static constexpr PartyQuestLineageRuntimeVersion Skyrim1597{1u, 5u, 97u, 0u};
    static constexpr PartyQuestLineageRuntimeVersion Skyrim161170{1u, 6u, 1170u, 0u};
    static constexpr PartyQuestLineageRuntimeVersion Skyrim1799{1u, 7u, 99u, 0u};
    static constexpr PartyQuestLineageRuntimeVersion Skyrim17104{1u, 7u, 104u, 0u};

    [[nodiscard]] static constexpr bool IsTarget(
        const PartyQuestLineageRuntimeVersion& acVersion) noexcept
    {
        return acVersion == Skyrim1597 ||
            acVersion == Skyrim161170 ||
            acVersion == Skyrim1799 ||
            acVersion == Skyrim17104;
    }

    [[nodiscard]] static constexpr bool AllowsProvider(
        const PartyQuestLineageRuntimeVersion& acVersion,
        PartyQuestLineageProviderKind aKind) noexcept
    {
        if (!IsTarget(acVersion))
            return false;

        if (aKind == PartyQuestLineageProviderKind::SkseCosave)
            return true;

        // An embedded provider remains an intended no-SKSE fallback, but it is
        // unapproved until a concrete exact-runtime implementation and live
        // evidence exist. Merely naming the kind must not grant authority.
        return false;
    }

    [[nodiscard]] static constexpr bool IsApprovedDescriptor(
        const PartyQuestLineageProviderDescriptor& acDescriptor,
        const PartyQuestLineageRuntimeVersion& acExpectedRuntime) noexcept
    {
        const PartyQuestLineageRuntimeVersion describedRuntime{
            acDescriptor.RuntimeMajor,
            acDescriptor.RuntimeMinor,
            acDescriptor.RuntimePatch,
            acDescriptor.RuntimeBuild};
        const auto kind = static_cast<PartyQuestLineageProviderKind>(
            acDescriptor.ProviderKind);

        return acDescriptor.AbiVersion ==
                kPartyQuestLineageProviderAbiVersion &&
            acDescriptor.StructSize ==
                static_cast<uint32_t>(sizeof(PartyQuestLineageProviderDescriptor)) &&
            acDescriptor.Reserved0 == 0u &&
            acDescriptor.Reserved1 == 0u &&
            acDescriptor.Reserved2 == 0u &&
            describedRuntime == acExpectedRuntime &&
            AllowsProvider(describedRuntime, kind) &&
            acDescriptor.ProviderFingerprint ==
                kPartyQuestSkseCosaveProviderFingerprintV2 &&
            (acDescriptor.Capabilities &
                kPartyQuestRequiredLineageProviderCapabilities) ==
                kPartyQuestRequiredLineageProviderCapabilities &&
            (acDescriptor.Capabilities &
                ~kPartyQuestRequiredLineageProviderCapabilities) == 0u;
    }
};
