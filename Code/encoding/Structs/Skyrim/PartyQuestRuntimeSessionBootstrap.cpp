#include <Structs/Skyrim/PartyQuestRuntimeSessionBootstrap.h>

#include <Structs/Skyrim/PartyQuestCoopSaveLayout.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>

PartyQuestRuntimeSessionBootstrapResult
PartyQuestRuntimeSessionBootstrap::BindProcessOwner(
    const std::filesystem::path& acCoopReplicaRoot,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileLineageAuthorization& acPlayerProfile) noexcept
{
    PartyQuestRuntimeSessionBootstrapResult result;

    if (!acCampaignId.IsValid())
    {
        result.Status = PartyQuestRuntimeSessionBootstrapStatus::InvalidCampaign;
        return result;
    }

    if (!acPlayerProfile.IsVerified())
    {
        result.Status =
            PartyQuestRuntimeSessionBootstrapStatus::UnverifiedPlayerProfile;
        return result;
    }

    auto& generationFence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    auto generationLease = generationFence.TryAcquire(
        acPlayerProfile.GetRuntimeGeneration());
    if (!generationLease || !generationLease->IsValid())
    {
        result.Status =
            PartyQuestRuntimeSessionBootstrapStatus::RuntimeGenerationUnavailable;
        return result;
    }

    try
    {
        if (acCoopReplicaRoot.empty() || !acCoopReplicaRoot.is_absolute())
        {
            result.Status =
                PartyQuestRuntimeSessionBootstrapStatus::InvalidReplicaRoot;
            return result;
        }

        const auto paths = PartyQuestCoopSaveLayout::Build(
            acCoopReplicaRoot,
            acCampaignId,
            acPlayerProfile.GetProfileId());
        if (!paths ||
            !PartyQuestCoopSaveLayout::Matches(
                *paths,
                acCampaignId,
                acPlayerProfile.GetProfileId()))
        {
            result.Status = PartyQuestRuntimeSessionBootstrapStatus::InvalidLayout;
            return result;
        }

        // Keep generationLease alive through the complete synchronous Bind().
        // Lifecycle invalidation requires the exclusive side of the same fence,
        // so no character/runtime transition can cross this publication point.
        auto& owner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
        result.Owner = owner.Bind(
            acCampaignId,
            acPlayerProfile.GetProfileId(),
            *paths);
        result.Status = result.Owner.IsBound()
            ? PartyQuestRuntimeSessionBootstrapStatus::Bound
            : PartyQuestRuntimeSessionBootstrapStatus::OwnerRejected;
        return result;
    }
    catch (...)
    {
        result.Status = PartyQuestRuntimeSessionBootstrapStatus::InvalidLayout;
        return result;
    }
}
