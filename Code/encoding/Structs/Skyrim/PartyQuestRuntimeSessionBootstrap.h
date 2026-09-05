#pragma once

#include <Structs/Skyrim/PartyQuestPlayerProfileLineage.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>

#include <cstdint>
#include <filesystem>

class PartyQuestRuntimeSessionBootstrapTestAccess;

/**
 * Fail-closed result for binding the shared process runtime owner from a proven
 * current-character profile identity.
 */
enum class PartyQuestRuntimeSessionBootstrapStatus : uint8_t
{
    Bound,
    InvalidCampaign,
    UnverifiedPlayerProfile,
    RuntimeGenerationUnavailable,
    InvalidReplicaRoot,
    InvalidLayout,
    LifecycleCoverageIncomplete,
    OwnerRejected
};

struct PartyQuestRuntimeSessionBootstrapResult
{
    PartyQuestRuntimeSessionBootstrapStatus Status{
        PartyQuestRuntimeSessionBootstrapStatus::UnverifiedPlayerProfile};
    PartyQuestRuntimeSessionOwnerBindResult Owner;

    [[nodiscard]] bool IsBound() const noexcept
    {
        return Status == PartyQuestRuntimeSessionBootstrapStatus::Bound &&
            Owner.IsBound();
    }
};

/**
 * Production bootstrap authority for the shared process runtime owner.
 *
 * Production integration must arrive here with an unforgeable character-lineage
 * authorization and complete verified pre-transition coverage for Load Game,
 * New Game and return-to-Main-Menu. The exact runtime generation is pinned for
 * the complete layout + owner bind so a lifecycle transition cannot race
 * profile verification and session hydration.
 *
 * Lifecycle coverage is production-visible only after the native hook validator
 * proves that every required pre-transition target was actually committed and
 * enabled. Unsupported/mismatched runtimes therefore remain fail-closed with
 * LifecycleCoverageIncomplete rather than turning queued hook intent into
 * bootstrap authority.
 *
 * This class does not discover a Skyrim character identity, generate a profile
 * id, infer identity from a save filename, or hook any engine lifecycle source.
 */
class PartyQuestRuntimeSessionBootstrap final
{
public:
    [[nodiscard]] static PartyQuestRuntimeSessionBootstrapResult BindProcessOwner(
        const std::filesystem::path& acCoopReplicaRoot,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileLineageAuthorization& acPlayerProfile) noexcept;

private:
    [[nodiscard]] static PartyQuestRuntimeSessionBootstrapResult BindProcessOwnerInternal(
        const std::filesystem::path& acCoopReplicaRoot,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileLineageAuthorization& acPlayerProfile,
        bool aRequireCompleteLifecycleCoverage) noexcept;

    // Defined only in Code/tests; no production implementation/API exists.
    friend class PartyQuestRuntimeSessionBootstrapTestAccess;
};
