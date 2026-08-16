#pragma once

#include <Structs/Skyrim/PartyQuestPlayerProfileLineage.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>

#include <cstdint>
#include <filesystem>

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
 * This is intentionally narrower than PartyQuestRuntimeSessionOwner::Bind(),
 * which remains a low-level/testable lifetime primitive. Production integration
 * must arrive here with an unforgeable character-lineage authorization. The
 * exact runtime generation is pinned for the complete layout + owner bind so a
 * lifecycle transition cannot race profile verification and session hydration.
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
};
