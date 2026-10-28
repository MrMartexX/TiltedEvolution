#pragma once

#include <cstdint>
#include <cstddef>
#include <optional>

enum class PartyQuestNativeSaveProviderCapabilityBit : uint64_t
{
    ArtifactEvents = 1ull << 0u,
    RequestRetirement = 1ull << 1u,
    ExactRequestIdentity = 1ull << 2u,
    CheckedArtifactIo = 1ull << 3u,
    RequestWideDrain = 1ull << 4u
};

inline constexpr uint32_t kPartyQuestNativeSaveProviderDescriptorAbi = 1u;
inline constexpr uint32_t kPartyQuestNativeSaveEventAbi = 2u;
inline constexpr uint32_t kPartyQuestNativeSaveProviderImplementationVersion = 1u;
inline constexpr uint64_t kPartyQuestNativeSaveProviderFingerprint =
    0x3256455641535150ull; // "PQSAVEV2", deterministic identity, not a secret.
inline constexpr uint64_t kPartyQuestTask07ResearchProviderFingerprint =
    0x3148435253515051ull; // Exact research-only SKSE artifact identity.
inline constexpr uint64_t kPartyQuestTask07ResearchProviderMarker =
    0x594C4E4F48375254ull; // Must never be accepted by production policy.
inline constexpr uint64_t kPartyQuestRequiredNativeSaveProviderCapabilities =
    static_cast<uint64_t>(PartyQuestNativeSaveProviderCapabilityBit::ArtifactEvents) |
    static_cast<uint64_t>(PartyQuestNativeSaveProviderCapabilityBit::RequestRetirement) |
    static_cast<uint64_t>(PartyQuestNativeSaveProviderCapabilityBit::ExactRequestIdentity) |
    static_cast<uint64_t>(PartyQuestNativeSaveProviderCapabilityBit::CheckedArtifactIo) |
    static_cast<uint64_t>(PartyQuestNativeSaveProviderCapabilityBit::RequestWideDrain);

struct PartyQuestNativeSaveProviderDescriptor final
{
    uint32_t AbiVersion{};
    uint32_t StructSize{};
    uint32_t EventAbiVersion{};
    uint32_t ImplementationVersion{};
    uint64_t Capabilities{};
    uint32_t RuntimeMajor{};
    uint32_t RuntimeMinor{};
    uint32_t RuntimePatch{};
    uint32_t RuntimeBuild{};
    uint64_t ProviderFingerprint{};
    uint64_t Reserved0{};
    uint64_t Reserved1{};
};

static_assert(sizeof(PartyQuestNativeSaveProviderDescriptor) == 64u);
static_assert(offsetof(PartyQuestNativeSaveProviderDescriptor, AbiVersion) == 0u);
static_assert(offsetof(PartyQuestNativeSaveProviderDescriptor, StructSize) == 4u);
static_assert(offsetof(PartyQuestNativeSaveProviderDescriptor, EventAbiVersion) == 8u);
static_assert(offsetof(PartyQuestNativeSaveProviderDescriptor, ImplementationVersion) == 12u);
static_assert(offsetof(PartyQuestNativeSaveProviderDescriptor, Capabilities) == 16u);
static_assert(offsetof(PartyQuestNativeSaveProviderDescriptor, RuntimeMajor) == 24u);
static_assert(offsetof(PartyQuestNativeSaveProviderDescriptor, ProviderFingerprint) == 40u);
static_assert(offsetof(PartyQuestNativeSaveProviderDescriptor, Reserved0) == 48u);
static_assert(offsetof(PartyQuestNativeSaveProviderDescriptor, Reserved1) == 56u);

struct PartyQuestNativeSaveProviderPolicy final
{
    [[nodiscard]] static constexpr bool IsApprovedDescriptor(
        const PartyQuestNativeSaveProviderDescriptor& acDescriptor) noexcept
    {
        return acDescriptor.AbiVersion ==
                kPartyQuestNativeSaveProviderDescriptorAbi &&
            acDescriptor.StructSize == sizeof(PartyQuestNativeSaveProviderDescriptor) &&
            acDescriptor.EventAbiVersion == kPartyQuestNativeSaveEventAbi &&
            acDescriptor.ImplementationVersion ==
                kPartyQuestNativeSaveProviderImplementationVersion &&
            acDescriptor.Capabilities ==
                kPartyQuestRequiredNativeSaveProviderCapabilities &&
            acDescriptor.RuntimeMajor == 1u &&
            acDescriptor.RuntimeMinor == 6u &&
            acDescriptor.RuntimePatch == 1170u &&
            acDescriptor.RuntimeBuild == 0u &&
            acDescriptor.ProviderFingerprint ==
                kPartyQuestNativeSaveProviderFingerprint &&
            acDescriptor.Reserved0 == 0u && acDescriptor.Reserved1 == 0u;
    }

    [[nodiscard]] static constexpr bool IsApprovedTask07ResearchDescriptor(
        const PartyQuestNativeSaveProviderDescriptor& acDescriptor) noexcept
    {
        auto productionShape = acDescriptor;
        productionShape.ProviderFingerprint =
            kPartyQuestNativeSaveProviderFingerprint;
        productionShape.Reserved0 = 0u;
        return IsApprovedDescriptor(productionShape) &&
            acDescriptor.ProviderFingerprint ==
                kPartyQuestTask07ResearchProviderFingerprint &&
            acDescriptor.Reserved0 == kPartyQuestTask07ResearchProviderMarker;
    }

    [[nodiscard]] static constexpr bool IsApprovedBuildDescriptor(
        const PartyQuestNativeSaveProviderDescriptor& acDescriptor) noexcept
    {
#if defined(PARTYQUEST_TASK07_RESEARCH_ARTIFACT)
        return IsApprovedTask07ResearchDescriptor(acDescriptor);
#else
        return IsApprovedDescriptor(acDescriptor);
#endif
    }
};

class PartyQuestNativeSaveProviderRegistration;

class PartyQuestNativeSaveProviderToken final
{
public:
    PartyQuestNativeSaveProviderToken() noexcept = default;
    PartyQuestNativeSaveProviderToken(PartyQuestNativeSaveProviderToken&& aOther) noexcept;
    PartyQuestNativeSaveProviderToken& operator=(
        PartyQuestNativeSaveProviderToken&& aOther) noexcept;
    PartyQuestNativeSaveProviderToken(const PartyQuestNativeSaveProviderToken&) = delete;
    PartyQuestNativeSaveProviderToken& operator=(
        const PartyQuestNativeSaveProviderToken&) = delete;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] uint64_t GetRuntimeGeneration() const noexcept
    {
        return m_runtimeGeneration;
    }

private:
    friend class PartyQuestNativeSaveProviderRegistration;

    PartyQuestNativeSaveProviderToken(
        uint64_t aRegistrationId,
        uint64_t aRuntimeGeneration) noexcept;
    void Reset() noexcept;

    uint64_t m_registrationId{};
    uint64_t m_runtimeGeneration{};
    uint64_t m_providerFingerprint{};
};

enum class PartyQuestNativeSaveProviderRegistrationStatus : uint8_t
{
    Registered,
    Current,
    Busy,
    InvalidDescriptor,
    InvalidGeneration,
    Exhausted,
    Stale,
    Invalidated
};

struct PartyQuestNativeSaveProviderRegistrationResult
{
    PartyQuestNativeSaveProviderRegistrationStatus Status{
        PartyQuestNativeSaveProviderRegistrationStatus::InvalidDescriptor};
    std::optional<PartyQuestNativeSaveProviderToken> Token;
};

/**
 * Pure, externally serialized registration state. RegisterAuthenticated has a
 * strict trusted-loader precondition: descriptor bytes and fingerprint are
 * compatibility identity, not proof of module origin. This class performs no
 * module loading, callback registration, unregister or shutdown quiescence.
 */
class PartyQuestNativeSaveProviderRegistration final
{
public:
    [[nodiscard]] PartyQuestNativeSaveProviderRegistrationResult
    RegisterAuthenticated(
        const PartyQuestNativeSaveProviderDescriptor& acDescriptor,
        uint64_t aRuntimeGeneration) noexcept;

    [[nodiscard]] PartyQuestNativeSaveProviderRegistrationStatus Validate(
        const PartyQuestNativeSaveProviderToken& acToken,
        uint64_t aRuntimeGeneration) const noexcept;

    [[nodiscard]] PartyQuestNativeSaveProviderRegistrationStatus Invalidate(
        const PartyQuestNativeSaveProviderToken& acToken) noexcept;

private:
    uint64_t m_registrationId{};
    uint64_t m_runtimeGeneration{};
    uint64_t m_nextRegistrationId{1};
    bool m_active{};
};
