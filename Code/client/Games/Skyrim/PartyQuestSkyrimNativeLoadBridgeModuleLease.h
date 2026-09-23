#pragma once

#include <Structs/Skyrim/PartyQuestNativeLoadBridgeCallCapability.h>

#include <cstdint>
#include <type_traits>

class PartyQuestSkyrimNativeLoadBridgeResolver;
class PartyQuestSkyrimNativeLoadBridgeOwner;
class PartyQuestSkyrimNativeLoadBridgeModuleLeaseTestAccess;

enum class PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus : uint8_t
{
    Ready = 1u,
    InvalidArgument = 2u,
    ModuleReferenceFailed = 3u,
    ExportModuleMismatch = 4u,
    ModulePinFailed = 5u
};

/**
 * Move-only physical call lease for one already-authenticated native LoadGame
 * bridge binding.
 *
 * This type is deliberately not a resolver. Only the future trusted resolver
 * (and tests) may construct it. Construction proves that all five accepted
 * exports belong to one already-loaded executable image and permanently pins
 * that image for process lifetime before publishing the lease.
 *
 * The lease is bound to the resolver-authenticated runtime generation and
 * runtime fingerprint. It does not turn those values into runtime authority:
 * CurrentGeneration calls still require the concrete owner to hold the exact
 * PartyQuestRuntimeGenerationFence execution lease. RequestDrain calls require
 * no old-generation execution lease and remain limited by the portable
 * capability to exact Cancel/Poll/Retire work for the retained request.
 *
 * Execute() accepts only copied POD effect/capability values. Export pointers
 * are private and never borrowed by callers. C++ exceptions and Windows SEH are
 * contained around every foreign call. An uncertain foreign result poisons the
 * logical lease but never unpins the module.
 *
 * Caller serialization is required. This class owns no mutex and invokes no
 * callbacks.
 */
class PartyQuestSkyrimNativeLoadBridgeModuleLease final
{
public:
    PartyQuestSkyrimNativeLoadBridgeModuleLease(
        PartyQuestSkyrimNativeLoadBridgeModuleLease&& aOther) noexcept;
    PartyQuestSkyrimNativeLoadBridgeModuleLease& operator=(
        PartyQuestSkyrimNativeLoadBridgeModuleLease&&) = delete;

    PartyQuestSkyrimNativeLoadBridgeModuleLease(
        const PartyQuestSkyrimNativeLoadBridgeModuleLease&) = delete;
    PartyQuestSkyrimNativeLoadBridgeModuleLease& operator=(
        const PartyQuestSkyrimNativeLoadBridgeModuleLease&) = delete;

    ~PartyQuestSkyrimNativeLoadBridgeModuleLease() noexcept = default;

    [[nodiscard]] bool IsPinned() const noexcept
    {
        return m_module != nullptr;
    }

    [[nodiscard]] bool IsCallable() const noexcept
    {
        return IsPinned() && m_poisoned == 0u &&
            m_reserve != nullptr && m_cancel != nullptr &&
            m_poll != nullptr && m_retire != nullptr &&
            m_boundGeneration != 0u && m_runtimeFingerprint != 0u;
    }

    [[nodiscard]] bool IsPoisoned() const noexcept
    {
        return m_poisoned != 0u;
    }

    [[nodiscard]] uint64_t GetBoundGeneration() const noexcept
    {
        return m_boundGeneration;
    }

    [[nodiscard]] uint64_t GetRuntimeFingerprint() const noexcept
    {
        return m_runtimeFingerprint;
    }

private:
    friend class PartyQuestSkyrimNativeLoadBridgeResolver;
    friend class PartyQuestSkyrimNativeLoadBridgeOwner;
    friend class PartyQuestSkyrimNativeLoadBridgeModuleLeaseTestAccess;

    [[nodiscard]] PartyQuestNativeLoadBridgeOwnerForeignOutcome Execute(
        const PartyQuestNativeLoadBridgeCallCapability& acCapability,
        const PartyQuestNativeLoadBridgeOwnerEffect& acEffect) noexcept;

    PartyQuestSkyrimNativeLoadBridgeModuleLease() noexcept = default;

    [[nodiscard]] static PartyQuestSkyrimNativeLoadBridgeModuleLease
    CreateAuthenticated(
        void* apExpectedModule,
        uint64_t aBoundGeneration,
        uint64_t aRuntimeFingerprint,
        PartyQuestNativeLoadBridgeGetDescriptorExport apGetDescriptor,
        PartyQuestNativeLoadBridgeReserveExport apReserve,
        PartyQuestNativeLoadBridgeCancelExport apCancel,
        PartyQuestNativeLoadBridgePollExport apPoll,
        PartyQuestNativeLoadBridgeRetireExport apRetire,
        PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus&
            aStatus) noexcept;

    [[nodiscard]] bool Matches(
        const PartyQuestNativeLoadBridgeCallCapability& acCapability,
        const PartyQuestNativeLoadBridgeOwnerEffect& acEffect) const noexcept;

    [[nodiscard]] PartyQuestNativeLoadBridgeOwnerForeignOutcome FailUnknown(
        const PartyQuestNativeLoadBridgeOwnerEffect& acEffect) noexcept;

    void ResetMovedFrom() noexcept;

    void* m_module{};
    PartyQuestNativeLoadBridgeReserveExport m_reserve{};
    PartyQuestNativeLoadBridgeCancelExport m_cancel{};
    PartyQuestNativeLoadBridgePollExport m_poll{};
    PartyQuestNativeLoadBridgeRetireExport m_retire{};

    uint64_t m_boundGeneration{};
    uint64_t m_runtimeFingerprint{};
    uint8_t m_poisoned{};
};

static_assert(sizeof(
    PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus) == 1u);
static_assert(!std::is_default_constructible_v<
    PartyQuestSkyrimNativeLoadBridgeModuleLease>);
static_assert(!std::is_copy_constructible_v<
    PartyQuestSkyrimNativeLoadBridgeModuleLease>);
static_assert(!std::is_copy_assignable_v<
    PartyQuestSkyrimNativeLoadBridgeModuleLease>);
static_assert(std::is_nothrow_move_constructible_v<
    PartyQuestSkyrimNativeLoadBridgeModuleLease>);
static_assert(!std::is_move_assignable_v<
    PartyQuestSkyrimNativeLoadBridgeModuleLease>);
