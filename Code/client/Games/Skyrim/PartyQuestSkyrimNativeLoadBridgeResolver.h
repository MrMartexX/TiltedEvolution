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
    UnexpectedFailure = 17u
};

/**
 * Reviewed source evidence for one exact native LoadGame bridge artifact.
 *
 * This is intentionally stronger than a descriptor fingerprint: it binds the
 * bridge contract to one exact Skyrim runtime/executable identity, one relative
 * loaded-module path, one SHA-256 file identity and one expected runtime
 * fingerprint returned by the bridge descriptor.
 *
 * Production callers cannot mint this capability. The production registry is
 * deliberately empty until a native artifact has been source/live-reviewed.
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
                m_relativeModulePath.empty() ||
                m_relativeModulePath.is_absolute() ||
                m_relativeModulePath.has_root_name() ||
                m_relativeModulePath.has_root_directory() ||
                m_runtimeFingerprint == 0u)
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
        catch (...)
        {
            return false;
        }
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
        , m_reviewed(aReviewed)
    {
    }

    PartyQuestSkyrimRuntimeVersion m_runtimeVersion{};
    PartyQuestSkyrimExecutableIdentity m_executableIdentity{};
    std::filesystem::path m_relativeModulePath;
    std::array<uint8_t, 32> m_moduleSha256{};
    uint64_t m_runtimeFingerprint{};
    bool m_reviewed{};
};

/**
 * Production registry of reviewed native LoadGame bridge artifacts.
 *
 * No entry is intentionally published yet. Returning an invalid authorization
 * is the executable form of the current blocker: repository evidence does not
 * establish a trusted module artifact/hash/runtime fingerprint for the bridge.
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
 * Authenticates and pins one already-loaded native LoadGame bridge.
 *
 * Trust is conjunctive:
 * - verified launcher/VersionDb runtime identity;
 * - reviewed source authorization for the exact runtime/executable/module hash;
 * - exact final path, FILE_ID_INFO and reviewed SHA-256 under the caller's
 *   trusted game root;
 * - every required export belongs to that same executable image;
 * - exact process generation execution lease;
 * - SEH-contained descriptor call and exact descriptor/fingerprint policy.
 *
 * The resolver never LoadLibrary()s a candidate and never invents module names,
 * hashes or runtime fingerprints. It performs no LoadGame hook/service wiring.
 *
 * The disk-file SHA-256 is reviewed backing-file evidence, not mapped-page
 * attestation. Source/live review must still establish that the accepted native
 * artifact cannot be replaced after load in a way that invalidates that
 * assumption. The permanent module pin only proves code lifetime after
 * acceptance.
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

private:
    friend class PartyQuestSkyrimNativeLoadBridgeResolverTestAccess;

    [[nodiscard]] static PartyQuestSkyrimNativeLoadBridgeResolveResult
    ResolveAndPin(
        const std::filesystem::path& acTrustedGameDirectory,
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
