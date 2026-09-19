#pragma once

#include <Structs/Skyrim/PartyQuestAsyncSaveContract.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

struct PartyQuestNativeSaveRequestIdentity final
{
    uint64_t RuntimeGeneration{};
    uint64_t TransactionId{};
    uint64_t TargetWorldRevision{};
    uint64_t CaptureEpochId{};
    uint64_t AttemptNonce{};
    std::array<char, 33> CampaignId{};
    std::array<char, 33> PlayerProfileId{};
    std::array<char, 96> SaveName{};
    std::array<uint8_t, 6> Reserved{};
};

struct PartyQuestNativeIsolatedSaveRequest final
{
    uint32_t AbiVersion{};
    uint32_t StructSize{};
    PartyQuestNativeSaveRequestIdentity Identity;
    std::array<char, 260> RelativeSavePath{};
};

static_assert(sizeof(PartyQuestNativeSaveRequestIdentity) == 208u);
static_assert(sizeof(PartyQuestNativeIsolatedSaveRequest) == 480u);
static_assert(alignof(PartyQuestNativeIsolatedSaveRequest) == 8u);
static_assert(std::is_standard_layout_v<PartyQuestNativeSaveRequestIdentity>);
static_assert(std::is_trivially_copyable_v<PartyQuestNativeSaveRequestIdentity>);
static_assert(std::is_standard_layout_v<PartyQuestNativeIsolatedSaveRequest>);
static_assert(std::is_trivially_copyable_v<PartyQuestNativeIsolatedSaveRequest>);
static_assert(offsetof(PartyQuestNativeSaveRequestIdentity, RuntimeGeneration) == 0u);
static_assert(offsetof(PartyQuestNativeSaveRequestIdentity, TransactionId) == 8u);
static_assert(offsetof(PartyQuestNativeSaveRequestIdentity, TargetWorldRevision) == 16u);
static_assert(offsetof(PartyQuestNativeSaveRequestIdentity, CaptureEpochId) == 24u);
static_assert(offsetof(PartyQuestNativeSaveRequestIdentity, AttemptNonce) == 32u);
static_assert(offsetof(PartyQuestNativeSaveRequestIdentity, CampaignId) == 40u);
static_assert(offsetof(PartyQuestNativeSaveRequestIdentity, PlayerProfileId) == 73u);
static_assert(offsetof(PartyQuestNativeSaveRequestIdentity, SaveName) == 106u);
static_assert(offsetof(PartyQuestNativeSaveRequestIdentity, Reserved) == 202u);
static_assert(offsetof(PartyQuestNativeIsolatedSaveRequest, AbiVersion) == 0u);
static_assert(offsetof(PartyQuestNativeIsolatedSaveRequest, StructSize) == 4u);
static_assert(offsetof(PartyQuestNativeIsolatedSaveRequest, Identity) == 8u);
static_assert(offsetof(PartyQuestNativeIsolatedSaveRequest, RelativeSavePath) == 216u);

enum class PartyQuestNativeSaveRequestEncodeStatus : uint8_t
{
    Encoded,
    InvalidIdentity,
    EncodingFailed
};

struct PartyQuestNativeSaveRequestEncodeResult final
{
    PartyQuestNativeSaveRequestEncodeStatus Status{
        PartyQuestNativeSaveRequestEncodeStatus::InvalidIdentity};
    std::optional<PartyQuestNativeIsolatedSaveRequest> Request;
};

/**
 * Produces the exact fixed Skyrim/SKSE provider request ABI without accepting
 * a caller-controlled path. The relative path is derived solely from the
 * validated campaign/profile identity and is always the isolated saves root.
 * This is serialization only; it grants no provider or engine authority.
 */
class PartyQuestNativeSaveRequestEncoder final
{
public:
    static constexpr uint32_t kAbiVersion = 2u;

    [[nodiscard]] static PartyQuestNativeSaveRequestEncodeResult Encode(
        const PartyQuestAsyncSaveRequestIdentity& acIdentity) noexcept;
};
