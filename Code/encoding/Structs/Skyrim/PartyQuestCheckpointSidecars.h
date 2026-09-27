#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

class PartyQuestCheckpointSidecarPolicy;

enum class PartyQuestCheckpointSidecarRequirementMode : uint8_t
{
    Required,
    Optional
};

/**
 * Local temporal-consistency contract implemented by a trusted sidecar
 * provider. This is intentionally not part of the network manifest: the server
 * requires restorable capability identity, while the client chooses how its
 * local adapter obtains a coherent snapshot.
 *
 * AtomicSnapshot means the provider can materialize one immutable logical state
 * without requiring an externally held freeze. FrozenUntilEpochRelease means
 * the provider requires a real freeze lease spanning the logical checkpoint
 * capture epoch. That lease/release orchestration is not wired yet, so the
 * production collector currently accepts AtomicSnapshot only and fails closed
 * for FrozenUntilEpochRelease. Unspecified is diagnostic only.
 */
enum class PartyQuestCheckpointSidecarCaptureConsistency : uint8_t
{
    Unspecified,
    AtomicSnapshot,
    FrozenUntilEpochRelease
};

/**
 * Network/campaign-safe description of one external state capability that may
 * participate in a save checkpoint. It intentionally contains no filesystem
 * path. A server/profile can require a capability, but cannot make a client read
 * an arbitrary local file.
 *
 * A non-zero RestoreAdapterFingerprint is mandatory even for capture: critical
 * repair must not accept a sidecar that can be backed up but cannot be restored
 * to its live owner after a crash.
 */
struct PartyQuestCheckpointSidecarRequirement
{
    uint64_t CapabilityId{};
    uint32_t SchemaVersion{};
    uint64_t ProviderFingerprint{};
    uint64_t RestoreAdapterFingerprint{};
    PartyQuestCheckpointSidecarRequirementMode Mode{
        PartyQuestCheckpointSidecarRequirementMode::Required};

    bool operator==(const PartyQuestCheckpointSidecarRequirement&) const noexcept = default;
};

/**
 * Local evidence registered by a trusted sidecar provider/native adapter. No
 * path is carried here; path binding is a separate local-only layer.
 *
 * CaptureConsistency is a provider contract, not a timestamp label. Production
 * epoch-bound checkpoint capture currently accepts only AtomicSnapshot.
 * Unspecified remains available for legacy diagnostic capability checks;
 * FrozenUntilEpochRelease remains represented so future freeze orchestration can
 * be added without pretending the capability is already safe today.
 */
struct PartyQuestCheckpointSidecarFacts
{
    uint64_t CapabilityId{};
    uint32_t SchemaVersion{};
    uint64_t ProviderFingerprint{};
    uint64_t RestoreAdapterFingerprint{};
    PartyQuestCheckpointSidecarCaptureConsistency CaptureConsistency{
        PartyQuestCheckpointSidecarCaptureConsistency::Unspecified};
    bool CaptureAvailable{};
    bool RestoreAvailable{};

    bool operator==(const PartyQuestCheckpointSidecarFacts&) const noexcept = default;
};

enum class PartyQuestCheckpointSidecarStatus : uint8_t
{
    Authorized,
    OptionalUnavailable,
    RequiredUnavailable,
    InvalidRequirement,
    InvalidFacts,
    CapabilityMismatch,
    SchemaVersionMismatch,
    ProviderMismatch,
    RestoreAdapterMismatch,
    CaptureUnavailable,
    RestoreUnavailable
};

/**
 * Capability authorization token. Callers can only construct an unverified
 * default token; exact verified capability identity and the trusted provider's
 * local capture-consistency contract are issued by the policy.
 */
class PartyQuestCheckpointSidecarAuthorization final
{
public:
    PartyQuestCheckpointSidecarAuthorization() noexcept = default;

    [[nodiscard]] bool IsVerified() const noexcept { return m_verified; }
    [[nodiscard]] uint64_t GetCapabilityId() const noexcept { return m_capabilityId; }
    [[nodiscard]] uint32_t GetSchemaVersion() const noexcept { return m_schemaVersion; }
    [[nodiscard]] uint64_t GetProviderFingerprint() const noexcept { return m_providerFingerprint; }
    [[nodiscard]] uint64_t GetRestoreAdapterFingerprint() const noexcept
    {
        return m_restoreAdapterFingerprint;
    }
    [[nodiscard]] PartyQuestCheckpointSidecarCaptureConsistency GetCaptureConsistency() const noexcept
    {
        return m_captureConsistency;
    }
    [[nodiscard]] bool SupportsCoherentCapture() const noexcept
    {
        // Atomic providers are self-contained. Freeze-based providers are
        // intentionally rejected until a real epoch-scoped freeze lease and
        // release hook are part of the production capture orchestration.
        return m_verified &&
            m_captureConsistency == PartyQuestCheckpointSidecarCaptureConsistency::AtomicSnapshot;
    }
    [[nodiscard]] bool RequiresEpochFreeze() const noexcept
    {
        return m_verified &&
            m_captureConsistency == PartyQuestCheckpointSidecarCaptureConsistency::FrozenUntilEpochRelease;
    }

private:
    PartyQuestCheckpointSidecarAuthorization(
        uint64_t aCapabilityId,
        uint32_t aSchemaVersion,
        uint64_t aProviderFingerprint,
        uint64_t aRestoreAdapterFingerprint,
        PartyQuestCheckpointSidecarCaptureConsistency aCaptureConsistency) noexcept
        : m_capabilityId(aCapabilityId)
        , m_schemaVersion(aSchemaVersion)
        , m_providerFingerprint(aProviderFingerprint)
        , m_restoreAdapterFingerprint(aRestoreAdapterFingerprint)
        , m_captureConsistency(aCaptureConsistency)
        , m_verified(true)
    {
    }

    uint64_t m_capabilityId{};
    uint32_t m_schemaVersion{};
    uint64_t m_providerFingerprint{};
    uint64_t m_restoreAdapterFingerprint{};
    PartyQuestCheckpointSidecarCaptureConsistency m_captureConsistency{
        PartyQuestCheckpointSidecarCaptureConsistency::Unspecified};
    bool m_verified{};

    friend class PartyQuestCheckpointSidecarPolicy;
};

struct PartyQuestCheckpointSidecarDecision
{
    PartyQuestCheckpointSidecarStatus Status{
        PartyQuestCheckpointSidecarStatus::InvalidRequirement};
    PartyQuestCheckpointSidecarAuthorization Authorization;

    [[nodiscard]] bool IsAuthorized() const noexcept
    {
        return Status == PartyQuestCheckpointSidecarStatus::Authorized &&
            Authorization.IsVerified();
    }

    /** Optional absence does not block a checkpoint, but produces no file. */
    [[nodiscard]] bool IsSatisfied() const noexcept
    {
        return IsAuthorized() ||
            Status == PartyQuestCheckpointSidecarStatus::OptionalUnavailable;
    }
};

class PartyQuestCheckpointSidecarPolicy final
{
public:
    // Immutable client-side resource bounds. These values are deliberately not
    // carried by the campaign manifest, so remote policy cannot enlarge local
    // filesystem or allocation authority.
    static constexpr size_t MaxCapabilityCount = 64;
    static constexpr size_t MaxFilesPerCapability = 64;
    static constexpr size_t MaxRelativePathBytes = 1024;

    [[nodiscard]] static bool IsValidRequirement(
        const PartyQuestCheckpointSidecarRequirement& acRequirement) noexcept;

    [[nodiscard]] static bool IsValidFacts(
        const PartyQuestCheckpointSidecarFacts& acFacts) noexcept;

    /**
     * Evaluate one requirement. Passing nullptr is an explicit statement that
     * the local provider/capability is unavailable.
     *
     * A verified authorization can describe diagnostic-only Unspecified or a
     * future freeze-based provider, but production epoch-bound collection also
     * requires Authorization::SupportsCoherentCapture().
     */
    [[nodiscard]] static PartyQuestCheckpointSidecarDecision Evaluate(
        const PartyQuestCheckpointSidecarRequirement& acRequirement,
        const PartyQuestCheckpointSidecarFacts* apFacts) noexcept;
};

/**
 * Campaign-level exact requirement set. Duplicate capability ids and manifests
 * exceeding the immutable local capability bound are rejected.
 */
class PartyQuestCheckpointSidecarManifest final
{
public:
    bool AddRequirement(const PartyQuestCheckpointSidecarRequirement& acRequirement);

    [[nodiscard]] const PartyQuestCheckpointSidecarRequirement* FindRequirement(
        uint64_t aCapabilityId) const noexcept;

    /** Stable capability-id ordering for deterministic coverage/checkpoint plans. */
    [[nodiscard]] std::vector<PartyQuestCheckpointSidecarRequirement> GetRequirements() const;

    /**
     * Stable non-zero fingerprint of the authoritative requirement set. The
     * explicit empty manifest also has a non-zero fingerprint, so zero remains
     * reserved for "no contract was bound to this transaction".
     */
    [[nodiscard]] uint64_t ComputeFingerprint() const noexcept;

    [[nodiscard]] size_t GetRequirementCount() const noexcept
    {
        return m_requirements.size();
    }

private:
    std::unordered_map<uint64_t, PartyQuestCheckpointSidecarRequirement> m_requirements;
};
