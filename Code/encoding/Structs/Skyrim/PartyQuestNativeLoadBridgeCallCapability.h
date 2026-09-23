#pragma once

#include <Structs/Skyrim/PartyQuestNativeLoadBridgeOwnerState.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

enum class PartyQuestNativeLoadBridgeCallAuthority : uint8_t
{
    None = 0u,
    CurrentGeneration = 1u,
    RequestDrain = 2u
};

enum class PartyQuestNativeLoadBridgeCallCapabilityStatus : uint8_t
{
    Authorized = 1u,
    NoForeignEffect = 2u,
    InvalidPlan = 3u,
    CapabilityUnavailable = 4u,
    StateMismatch = 5u,
    NonceMismatch = 6u
};

/**
 * Fixed POD authorization for exactly one already-planned native load bridge
 * foreign call.
 *
 * CurrentGeneration means the concrete client owner must additionally hold the
 * exact PartyQuestRuntimeGenerationFence execution lease for BoundGeneration.
 *
 * RequestDrain is deliberately different authority. It exists only for the
 * exact Cancel/Poll/Retire operation of the reducer's already-active request
 * after admission has closed. It authorizes no runtime generation, no Reserve,
 * no callback and no mutation. The concrete platform owner must pair either
 * authority with its retained exact module pin; this portable type owns no
 * HMODULE, export pointer or borrowed address.
 *
 * The capability is copied out while the caller serializes OwnerState, then the
 * caller drops its mutex before executing foreign code. OwnerState's pending
 * effect prevents a second Plan until the exact foreign outcome is applied.
 */
struct PartyQuestNativeLoadBridgeCallCapability final
{
    PartyQuestNativeLoadBridgeOwnerEffectKind EffectKind{
        PartyQuestNativeLoadBridgeOwnerEffectKind::None};
    PartyQuestNativeLoadBridgeCallAuthority Authority{
        PartyQuestNativeLoadBridgeCallAuthority::None};
    uint8_t PinnedModuleRequired{};
    uint8_t Reserved[5]{};

    uint64_t BoundGeneration{};
    uint64_t ObservedGeneration{};
    uint64_t RuntimeFingerprint{};
    uint64_t AttemptNonce{};
    uint64_t EffectSequence{};

    // Populated only for Reserve. The reducer publishes a canonical identity:
    // Reserved0 and bytes outside Length are zero. Drain operations carry an
    // all-zero identity because their authority is the exact active nonce.
    PartyQuestNativeLoadBridgeIdentityV1 ReserveIdentity;

    [[nodiscard]] static constexpr bool IsZeroIdentity(
        const PartyQuestNativeLoadBridgeIdentityV1& acIdentity) noexcept
    {
        if (acIdentity.Length != 0u || acIdentity.Reserved0 != 0u)
            return false;

        for (const auto value : acIdentity.Bytes)
        {
            if (value != 0u)
                return false;
        }
        return true;
    }

    [[nodiscard]] static constexpr bool IsCanonicalIdentity(
        const PartyQuestNativeLoadBridgeIdentityV1& acIdentity) noexcept
    {
        if (!PartyQuestNativeLoadBridgePolicy::IsValidIdentity(acIdentity))
            return false;

        for (size_t index = acIdentity.Length;
             index < sizeof(acIdentity.Bytes);
             ++index)
        {
            if (acIdentity.Bytes[index] != 0u)
                return false;
        }
        return true;
    }

    [[nodiscard]] static constexpr bool IdentityEquals(
        const PartyQuestNativeLoadBridgeIdentityV1& acLeft,
        const PartyQuestNativeLoadBridgeIdentityV1& acRight) noexcept
    {
        if (!PartyQuestNativeLoadBridgePolicy::IsValidIdentity(acLeft) ||
            !PartyQuestNativeLoadBridgePolicy::IsValidIdentity(acRight) ||
            acLeft.Length != acRight.Length)
        {
            return false;
        }

        for (size_t index = 0u; index < acLeft.Length; ++index)
        {
            if (acLeft.Bytes[index] != acRight.Bytes[index])
                return false;
        }
        return true;
    }

    [[nodiscard]] constexpr bool IsAuthorized() const noexcept
    {
        if (PinnedModuleRequired != 1u ||
            BoundGeneration == 0u ||
            ObservedGeneration == 0u ||
            RuntimeFingerprint == 0u ||
            EffectSequence == 0u)
        {
            return false;
        }

        switch (EffectKind)
        {
        case PartyQuestNativeLoadBridgeOwnerEffectKind::Reserve:
            if (AttemptNonce != 0u ||
                !IsCanonicalIdentity(ReserveIdentity))
            {
                return false;
            }
            break;

        case PartyQuestNativeLoadBridgeOwnerEffectKind::Cancel:
        case PartyQuestNativeLoadBridgeOwnerEffectKind::Poll:
        case PartyQuestNativeLoadBridgeOwnerEffectKind::Retire:
            if (AttemptNonce == 0u ||
                !IsZeroIdentity(ReserveIdentity))
            {
                return false;
            }
            break;

        case PartyQuestNativeLoadBridgeOwnerEffectKind::None:
            return false;

        default:
            return false;
        }

        switch (Authority)
        {
        case PartyQuestNativeLoadBridgeCallAuthority::CurrentGeneration:
            return BoundGeneration == ObservedGeneration;

        case PartyQuestNativeLoadBridgeCallAuthority::RequestDrain:
            return EffectKind !=
                PartyQuestNativeLoadBridgeOwnerEffectKind::Reserve;

        case PartyQuestNativeLoadBridgeCallAuthority::None:
            return false;
        }

        return false;
    }

    [[nodiscard]] constexpr bool RequiresCurrentGenerationLease() const noexcept
    {
        return IsAuthorized() &&
            Authority ==
                PartyQuestNativeLoadBridgeCallAuthority::CurrentGeneration;
    }

    [[nodiscard]] constexpr bool IsRequestDrain() const noexcept
    {
        return IsAuthorized() &&
            Authority == PartyQuestNativeLoadBridgeCallAuthority::RequestDrain;
    }

    /**
     * RequestDrain never authorizes a runtime generation, including the old
     * generation recorded in BoundGeneration. That number is correlation
     * evidence only once drain authority has been issued.
     */
    [[nodiscard]] constexpr bool AuthorizesRuntimeGeneration(
        uint64_t aGeneration) const noexcept
    {
        return RequiresCurrentGenerationLease() &&
            aGeneration != 0u &&
            aGeneration == BoundGeneration;
    }

    /**
     * Non-Reserve operations are correlated exclusively by exact active nonce.
     * Reserve deliberately returns false here so a caller cannot authorize it
     * without also presenting the exact identity through AuthorizesExactReserve.
     */
    [[nodiscard]] constexpr bool AuthorizesExactCall(
        PartyQuestNativeLoadBridgeOwnerEffectKind aEffectKind,
        uint64_t aAttemptNonce) const noexcept
    {
        if (!IsAuthorized() ||
            aEffectKind != EffectKind ||
            EffectKind == PartyQuestNativeLoadBridgeOwnerEffectKind::Reserve)
        {
            return false;
        }

        return aAttemptNonce != 0u &&
            aAttemptNonce == AttemptNonce;
    }

    /**
     * Reserve is authorized only for the semantic identity that Plan()
     * published. Tail bytes outside Length are intentionally not identity by
     * ABI contract and therefore do not affect this comparison.
     */
    [[nodiscard]] constexpr bool AuthorizesExactReserve(
        const PartyQuestNativeLoadBridgeIdentityV1& acIdentity) const noexcept
    {
        return IsAuthorized() &&
            EffectKind ==
                PartyQuestNativeLoadBridgeOwnerEffectKind::Reserve &&
            IdentityEquals(ReserveIdentity, acIdentity);
    }
};

struct PartyQuestNativeLoadBridgeCallCapabilityResult final
{
    PartyQuestNativeLoadBridgeCallCapabilityStatus Status{
        PartyQuestNativeLoadBridgeCallCapabilityStatus::InvalidPlan};
    uint8_t HasCapability{};
    uint8_t Reserved[6]{};
    PartyQuestNativeLoadBridgeCallCapability Capability;

    [[nodiscard]] constexpr bool IsAuthorized() const noexcept
    {
        return Status ==
                PartyQuestNativeLoadBridgeCallCapabilityStatus::Authorized &&
            HasCapability == 1u &&
            Capability.IsAuthorized();
    }
};

/**
 * Portable policy binding one OwnerState Plan result to one foreign-call
 * authority.
 *
 * Call Authorize() with the OwnerState snapshot taken immediately after Plan()
 * while both are protected by the same caller-owned serialization lock.
 *
 * - Bound operations use CurrentGeneration and therefore still require the
 *   exact generation execution lease.
 * - DrainOnly/ShutdownDrain Cancel/Poll/Retire use RequestDrain. They remain
 *   callable after PartyQuestRuntimeGenerationFence advanced or completed its
 *   lifecycle ticket, but only for the exact already-active nonce and only
 *   while OwnerState retains the old capability.
 * - Reserve can never obtain RequestDrain authority.
 *
 * This policy does not release a module capability. The sole release decision
 * remains PartyQuestNativeLoadBridgeOwnerState::ApplyForeignOutcome().
 */
class PartyQuestNativeLoadBridgeCallCapabilityPolicy final
{
public:
    [[nodiscard]] static
    PartyQuestNativeLoadBridgeCallCapabilityResult Authorize(
        const PartyQuestNativeLoadBridgeOwnerSnapshot& acSnapshot,
        const PartyQuestNativeLoadBridgeOwnerResult& acPlan) noexcept;
};

static_assert(sizeof(PartyQuestNativeLoadBridgeCallAuthority) == 1u);
static_assert(sizeof(PartyQuestNativeLoadBridgeCallCapabilityStatus) == 1u);

static_assert(sizeof(PartyQuestNativeLoadBridgeCallCapability) == 312u);
static_assert(alignof(PartyQuestNativeLoadBridgeCallCapability) == 8u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeCallCapability, EffectKind) == 0u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeCallCapability, BoundGeneration) == 8u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeCallCapability, AttemptNonce) == 32u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeCallCapability, EffectSequence) == 40u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeCallCapability, ReserveIdentity) == 48u);

static_assert(sizeof(PartyQuestNativeLoadBridgeCallCapabilityResult) == 320u);
static_assert(alignof(PartyQuestNativeLoadBridgeCallCapabilityResult) == 8u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeCallCapabilityResult, Status) == 0u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeCallCapabilityResult, Capability) == 8u);

static_assert(std::is_standard_layout_v<
    PartyQuestNativeLoadBridgeCallCapability>);
static_assert(std::is_trivially_copyable_v<
    PartyQuestNativeLoadBridgeCallCapability>);
static_assert(std::is_standard_layout_v<
    PartyQuestNativeLoadBridgeCallCapabilityResult>);
static_assert(std::is_trivially_copyable_v<
    PartyQuestNativeLoadBridgeCallCapabilityResult>);
