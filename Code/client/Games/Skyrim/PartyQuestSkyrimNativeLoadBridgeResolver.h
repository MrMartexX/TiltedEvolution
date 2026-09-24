#pragma once

#include <Games/Skyrim/PartyQuestSkyrimNativeLoadBridgeOwner.h>
#include <Structs/Skyrim/PartyQuestSkyrimPapyrusRuntimeProfileResolver.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <utility>

class PartyQuestSkyrimNativeLoadBridgeResolverTestAccess;

enum class PartyQuestSkyrimNativeLoadBridgeResolveStatus : uint8_t
{
    Resolved = 1u,
    InvalidTrustedDirectory = 2u,
    RuntimeIdentityRejected = 3u,
    SourceAuthorizationRejected = 4u,
    RuntimeMismatch = 5u,
    ExecutableIdentityMismatch = 6u,
    ModuleUnavailable = 7u,
    ModulePathUnavailable = 8u,
    UnexpectedModulePath = 9u,
    ModuleHashMismatch = 10u,
    RequiredExportMissing = 11u,
    InvalidExportAddress = 12u,
    GenerationUnavailable = 13u,
    DescriptorCallFailed = 14u,
    DescriptorRejected = 15u,
    ModuleLeaseRejected = 16u,
    UnexpectedFailure = 17u,
    SourceKindMismatch = 18u,
    ProcessImageAddressRejected = 19u
};

/**
 * Reviewed source evidence for one exact native LoadGame bridge artifact.
 *
 * This is intentionally stronger than a descriptor fingerprint. It binds one
 * exact Skyrim runtime/executable identity and runtime fingerprint to one of
 * two disjoint reviewed code origins:
 *
 * - ExternalPinnedModule: exact relative module path plus SHA-256.
 * - ProcessImage: exact trusted pre-remap STR-image predicate plus exact direct
 *   descriptor/Reserve/Cancel/Poll/Retire call targets.
 *
 * Production callers cannot mint this capability. The production registry is
 * deliberately empty until the concrete native provider surface has been
 * source/live-reviewed.
 */
class PartyQuestSkyrimNativeLoadBridgeSourceAuthorization final
{
public:
    PartyQuestSkyrimNativeLoadBridgeSourceAuthorization() noexcept = default;

    [[nodiscard]] bool IsVerified() const noexcept
    {
        try
        {
            if (!m_reviewed ||
                m_runtimeVersion.Major != 1u ||
                m_runtimeVersion.Minor != 6u ||
                m_runtimeVersion.Patch != 1170u ||
                m_runtimeVersion.Build != 0u ||
                !m_executableIdentity.IsValid() ||
                m_runtimeFingerprint == 0u)
            {
                return false;
            }

            switch (m_lifetimeKind)
            {
            case PartyQuestSkyrimNativeLoadBridgeLeaseLifetimeKind::
                ExternalPinnedModule:
            {
                if (m_imageValidator ||
                    m_getDescriptor ||
                    m_reserve ||
                    m_cancel ||
                    m_poll ||
                    m_retire ||
                    m_relativeModulePath.empty() ||
                    m_relativeModulePath.is_absolute() ||
                    m_relativeModulePath.has_root_name() ||
                    m_relativeModulePath.has_root_directory())
                {
                    return false;
                }

                for (const auto& component : m_relativeModulePath)
                {
                    if (component == std::filesystem::path(".") ||
                        component == std::filesystem::path(".."))
                    {
                        return false;
                    }
                }

                bool hashNonzero = false;
                for (const auto value : m_moduleSha256)
                    hashNonzero = hashNonzero || value != 0u;
                return hashNonzero;
            }

            case PartyQuestSkyrimNativeLoadBridgeLeaseLifetimeKind::
                ProcessImage:
            {
                if (!m_relativeModulePath.empty())
                    return false;

                for (const auto value : m_moduleSha256)
                {
                    if (value != 0u)
                        return false;
                }

                return m_imageValidator &&
                    m_getDescriptor &&
                    m_reserve &&
                    m_cancel &&
                    m_poll &&
                    m_retire;
            }

            case PartyQuestSkyrimNativeLoadBridgeLeaseLifetimeKind::None:
                return false;
            }

            return false;
        }
        catch (...)
        {
            return false;
        }
    }

    [[nodiscard]] PartyQuestSkyrimNativeLoadBridgeLeaseLifetimeKind
    GetLifetimeKind() const noexcept
    {
        return m_lifetimeKind;
    }

    [[nodiscard]] const PartyQuestSkyrimRuntimeVersion&
    GetRuntimeVersion() const noexcept
    {
        return m_runtimeVersion;
    }

    [[nodiscard]] const PartyQuestSkyrimExecutableIdentity&
    GetExecutableIdentity() const noexcept
    {
        return m_executableIdentity;
    }

    [[nodiscard]] const std::filesystem::path&
    GetRelativeModulePath() const noexcept
    {
        return m_relativeModulePath;
    }

    [[nodiscard]] const std::array<uint8_t, 32>&
    GetModuleSha256() const noexcept
    {
        return m_moduleSha256;
    }

    [[nodiscard]] uint64_t GetRuntimeFingerprint() const noexcept
    {
        return m_runtimeFingerprint;
    }

    [[nodiscard]] PartyQuestSkyrimNativeLoadBridgeImageAddressValidator
    GetProcessImageValidator() const noexcept
    {
        return m_imageValidator;
    }

    [[nodiscard]] PartyQuestNativeLoadBridgeGetDescriptorExport
    GetProcessImageDescriptor() const noexcept
    {
        return m_getDescriptor;
    }

    [[nodiscard]] PartyQuestNativeLoadBridgeReserveExport
    GetProcessImageReserve() const noexcept
    {
        return m_reserve;
    }

    [[nodiscard]] PartyQuestNativeLoadBridgeCancelExport
    GetProcessImageCancel() const noexcept
    {
        return m_cancel;
    }

    [[nodiscard]] PartyQuestNativeLoadBridgePollExport
    GetProcessImagePoll() const noexcept
    {
        return m_poll;
    }

    [[nodiscard]] PartyQuestNativeLoadBridgeRetireExport
    GetProcessImageRetire() const noexcept
    {
        return m_retire;
    }

private:
    friend class PartyQuestSkyrimNativeLoadBridgeSourceRegistry;
    friend class PartyQuestSkyrimNativeLoadBridgeResolverTestAccess;

    PartyQuestSkyrimNativeLoadBridgeSourceAuthorization(
        PartyQuestSkyrimRuntimeVersion aRuntimeVersion,
        PartyQuestSkyrimExecutableIdentity aExecutableIdentity,
        std::filesystem::path aRelativeModulePath,
        std::array<uint8_t, 32> aModuleSha256,
        uint64_t aRuntimeFingerprint,
        bool aReviewed) noexcept
        : m_runtimeVersion(aRuntimeVersion)
        , m_executableIdentity(aExecutableIdentity)
        , m_relativeModulePath(std::move(aRelativeModulePath))
        , m_moduleSha256(aModuleSha256)
        , m_runtimeFingerprint(aRuntimeFingerprint)
        , m_lifetimeKind(
              PartyQuestSkyrimNativeLoadBridgeLeaseLifetimeKind::
                  ExternalPinnedModule)
        , m_reviewed(aReviewed)
    {
    }

    PartyQuestSkyrimNativeLoadBridgeSourceAuthorization(
        PartyQuestSkyrimRuntimeVersion aRuntimeVersion,
        PartyQuestSkyrimExecutableIdentity aExecutableIdentity,
        uint64_t aRuntimeFingerprint,
        PartyQuestSkyrimNativeLoadBridgeImageAddressValidator apImageValidator,
        PartyQuestNativeLoadBridgeGetDescriptorExport apGetDescriptor,
        PartyQuestNativeLoadBridgeReserveExport apReserve,
        PartyQuestNativeLoadBridgeCancelExport apCancel,
        PartyQuestNativeLoadBridgePollExport apPoll,
        PartyQuestNativeLoadBridgeRetireExport apRetire,
        bool aReviewed) noexcept
        : m_runtimeVersion(aRuntimeVersion)
        , m_executableIdentity(aExecutableIdentity)
        , m_runtimeFingerprint(aRuntimeFingerprint)
        , m_lifetimeKind(
              PartyQuestSkyrimNativeLoadBridgeLeaseLifetimeKind::
                  ProcessImage)
        , m_imageValidator(apImageValidator)
        , m_getDescriptor(apGetDescriptor)
        , m_reserve(apReserve)
        , m_cancel(apCancel)
        , m_poll(apPoll)
        , m_retire(apRetire)
        , m_reviewed(aReviewed)
    {
    }

    PartyQuestSkyrimRuntimeVersion m_runtimeVersion{};
    PartyQuestSkyrimExecutableIdentity m_executableIdentity{};
    std::filesystem::path m_relativeModulePath;
    std::array<uint8_t, 32> m_moduleSha256{};
    uint64_t m_runtimeFingerprint{};
    PartyQuestSkyrimNativeLoadBridgeLeaseLifetimeKind m_lifetimeKind{
        PartyQuestSkyrimNativeLoadBridgeLeaseLifetimeKind::None};

    PartyQuestSkyrimNativeLoadBridgeImageAddressValidator m_imageValidator{};
    PartyQuestNativeLoadBridgeGetDescriptorExport m_getDescriptor{};
    PartyQuestNativeLoadBridgeReserveExport m_reserve{};
    PartyQuestNativeLoadBridgeCancelExport m_cancel{};
    PartyQuestNativeLoadBridgePollExport m_poll{};
    PartyQuestNativeLoadBridgeRetireExport m_retire{};

    bool m_reviewed{};
};

/**
 * Production registry of reviewed native LoadGame bridge artifacts.
 *
 * No entry is intentionally published yet. Returning an invalid authorization
 * is the executable form of the current blocker: repository evidence has not
 * yet published one reviewed concrete provider surface (external module or
 * process image) with its exact runtime fingerprint.
 */
class PartyQuestSkyrimNativeLoadBridgeSourceRegistry final
{
public:
    [[nodiscard]] static PartyQuestSkyrimNativeLoadBridgeSourceAuthorization
    Resolve(
        const PartyQuestSkyrimRuntimeIdentityAuthorization&) noexcept
    {
        return {};
    }
};

struct PartyQuestSkyrimNativeLoadBridgeResolveResult final
{
    PartyQuestSkyrimNativeLoadBridgeResolveStatus Status{
        PartyQuestSkyrimNativeLoadBridgeResolveStatus::UnexpectedFailure};
    PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus LeaseStatus{
        PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus::
            InvalidArgument};
    PartyQuestNativeLoadBridgeDescriptorV1 Descriptor;
    std::optional<PartyQuestSkyrimNativeLoadBridgeModuleLease> Lease;

    [[nodiscard]] bool IsResolved() const noexcept
    {
        return Status ==
                PartyQuestSkyrimNativeLoadBridgeResolveStatus::Resolved &&
            LeaseStatus ==
                PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus::
                    Ready &&
            Lease && Lease->IsCallable();
    }
};

enum class PartyQuestSkyrimNativeLoadBridgeBindStatus : uint8_t
{
    Bound = 1u,
    ResolveRejected = 2u,
    OwnerRejected = 3u
};

struct PartyQuestSkyrimNativeLoadBridgeBindResult final
{
    PartyQuestSkyrimNativeLoadBridgeBindStatus Status{
        PartyQuestSkyrimNativeLoadBridgeBindStatus::ResolveRejected};
    PartyQuestSkyrimNativeLoadBridgeResolveStatus ResolverStatus{
        PartyQuestSkyrimNativeLoadBridgeResolveStatus::UnexpectedFailure};
    PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus LeaseStatus{
        PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus::
            InvalidArgument};
    uint8_t Reserved[5]{};

    PartyQuestNativeLoadBridgeDescriptorV1 Descriptor;
    PartyQuestSkyrimNativeLoadBridgeOwnerResult Owner;

    [[nodiscard]] bool IsBound() const noexcept
    {
        return Status ==
                PartyQuestSkyrimNativeLoadBridgeBindStatus::Bound &&
            ResolverStatus ==
                PartyQuestSkyrimNativeLoadBridgeResolveStatus::Resolved &&
            LeaseStatus ==
                PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus::
                    Ready &&
            Owner.Status ==
                PartyQuestSkyrimNativeLoadBridgeOwnerStatus::Applied &&
            Owner.State.Code ==
                PartyQuestNativeLoadBridgeOwnerResultCode::Bound;
    }
};

/**
 * Authenticates one native LoadGame bridge and turns it into an exact physical
 * call lease.
 *
 * Both origins require verified launcher/VersionDb runtime identity, reviewed
 * source authorization, exact process-generation execution authority,
 * SEH-contained descriptor validation and the exact runtime fingerprint.
 *
 * ExternalPinnedModule additionally requires exact path/FILE_ID/SHA-256 and
 * pins the loaded PE image. ProcessImage instead requires the reviewed
 * pre-remap STR-image address predicate to accept every exact direct call
 * target; the process image is intrinsically process-lifetime.
 *
 * The resolver never LoadLibrary()s a candidate and never invents module names,
 * hashes, direct targets or runtime fingerprints. It performs no LoadGame hook
 * or service wiring.
 */
class PartyQuestSkyrimNativeLoadBridgeResolver final
{
public:
    /**
     * Production handoff. No raw/pinned lease leaves the resolver boundary.
     * The owner independently reacquires exact generation authority before it
     * accepts the authenticated module lease.
     */
    [[nodiscard]] static PartyQuestSkyrimNativeLoadBridgeBindResult
    ResolveAndBind(
        const std::filesystem::path& acTrustedGameDirectory,
        const PartyQuestSkyrimRuntimeIdentityAuthorization& acRuntimeIdentity,
        const PartyQuestSkyrimNativeLoadBridgeSourceAuthorization& acSource,
        uint64_t aExpectedGeneration,
        PartyQuestSkyrimNativeLoadBridgeOwner& aOwner) noexcept;

    /**
     * Production registry entrypoint. Until a reviewed native artifact is
     * published, the empty source registry makes this fail closed before any
     * module lookup.
     */
    [[nodiscard]] static PartyQuestSkyrimNativeLoadBridgeBindResult
    ResolveReviewedAndBind(
        const std::filesystem::path& acTrustedGameDirectory,
        const PartyQuestSkyrimRuntimeIdentityAuthorization& acRuntimeIdentity,
        uint64_t aExpectedGeneration,
        PartyQuestSkyrimNativeLoadBridgeOwner& aOwner) noexcept;

    /**
     * Bind one reviewed in-process STR provider surface. Direct call targets
     * are part of acSource authorization; callers cannot substitute them.
     */
    [[nodiscard]] static PartyQuestSkyrimNativeLoadBridgeBindResult
    ResolveProcessImageAndBind(
        const PartyQuestSkyrimRuntimeIdentityAuthorization& acRuntimeIdentity,
        const PartyQuestSkyrimNativeLoadBridgeSourceAuthorization& acSource,
        uint64_t aExpectedGeneration,
        PartyQuestSkyrimNativeLoadBridgeOwner& aOwner) noexcept;

    [[nodiscard]] static PartyQuestSkyrimNativeLoadBridgeBindResult
    ResolveReviewedProcessImageAndBind(
        const PartyQuestSkyrimRuntimeIdentityAuthorization& acRuntimeIdentity,
        uint64_t aExpectedGeneration,
        PartyQuestSkyrimNativeLoadBridgeOwner& aOwner) noexcept;

private:
    friend class PartyQuestSkyrimNativeLoadBridgeResolverTestAccess;

    [[nodiscard]] static PartyQuestSkyrimNativeLoadBridgeResolveResult
    ResolveAndPin(
        const std::filesystem::path& acTrustedGameDirectory,
        const PartyQuestSkyrimRuntimeIdentityAuthorization& acRuntimeIdentity,
        const PartyQuestSkyrimNativeLoadBridgeSourceAuthorization& acSource,
        uint64_t aExpectedGeneration) noexcept;

    [[nodiscard]] static PartyQuestSkyrimNativeLoadBridgeResolveResult
    ResolveProcessImageAndPin(
        const PartyQuestSkyrimRuntimeIdentityAuthorization& acRuntimeIdentity,
        const PartyQuestSkyrimNativeLoadBridgeSourceAuthorization& acSource,
        uint64_t aExpectedGeneration) noexcept;

    [[nodiscard]] static PartyQuestSkyrimNativeLoadBridgeBindResult
    BindResolved(
        PartyQuestSkyrimNativeLoadBridgeOwner& aOwner,
        PartyQuestSkyrimNativeLoadBridgeResolveResult&& aResolved) noexcept;

    [[nodiscard]] static bool HashFileSha256(
        const std::filesystem::path& acPath,
        std::array<uint8_t, 32>& aHash) noexcept;
};

static_assert(sizeof(PartyQuestSkyrimNativeLoadBridgeResolveStatus) == 1u);
static_assert(sizeof(PartyQuestSkyrimNativeLoadBridgeBindStatus) == 1u);
