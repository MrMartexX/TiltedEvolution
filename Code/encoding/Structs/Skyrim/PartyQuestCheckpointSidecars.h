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
 * the provider contract requires its live state to remain quiesced/frozen until
 * the logical checkpoint capture epoch is released. Unspecified is diagnostic
 * only and is rejected by the production epoch-bound mirror collector.
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
 * epoch-bound checkpoint capture accepts only AtomicSnapshot or
 * FrozenUntilEpochRelease. Unspecified remains available for legacy diagnostic
 * capability checks that cannot cross the production PreRepair assembler.
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
        return m_verified &&
            (m_captureConsistency == PartyQuestCheckpointSidecarCaptureConsistency::AtomicSnapshot ||
             m_captureConsistency == PartyQuestCheckpointSidecarCaptureConsistency::FrozenUntilEpochRelease);
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
    [[nodiscard]] static bool IsValidRequirement(
        const PartyQuestCheckpointSidecarRequirement& acRequirement) noexcept;

    [[nodiscard]] static bool IsValidFacts(
        const PartyQuestCheckpointSidecarFacts& acFacts) noexcept;

    /**
     * Evaluate one requirement. Passing nullptr is an explicit statement that
     * the local provider/capability is unavailable.
     *
     * A verified authorization with Unspecified consistency is intentionally
     * usable only by legacy diagnostics. Production epoch-bound collection also
     * requires Authorization::SupportsCoherentCapture().
     */
    [[nodiscard]] static PartyQuestCheckpointSidecarDecision Evaluate(
        const PartyQuestCheckpointSidecarRequirement& acRequirement,
        const PartyQuestCheckpointSidecarFacts* apFacts) noexcept;
};

/** Campaign-level exact requirement set. Duplicate capability ids are rejected. */
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
