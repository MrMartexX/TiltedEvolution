#include <Structs/Skyrim/PartyQuestRuntimeSessionBootstrap.h>

#include <Structs/Skyrim/PartyQuestCoopSaveLayout.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeLifecycleIntegration.h>

PartyQuestRuntimeSessionBootstrapResult
PartyQuestRuntimeSessionBootstrap::BindProcessOwner(
    const std::filesystem::path& acCoopReplicaRoot,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileLineageAuthorization& acPlayerProfile) noexcept
{
    return BindProcessOwnerInternal(
        acCoopReplicaRoot,
        acCampaignId,
        acPlayerProfile,
        true);
}

PartyQuestRuntimeSessionBootstrapResult
PartyQuestRuntimeSessionBootstrap::BindProcessOwnerInternal(
    const std::filesystem::path& acCoopReplicaRoot,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileLineageAuthorization& acPlayerProfile,
    bool aRequireCompleteLifecycleCoverage) noexcept
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

        // P0-C: a correct lineage token is not enough while identity-changing
        // engine transitions can bypass the owner/generation fence. Keep this
        // check immediately before publication so all ordinary input/generation
        // validation still fails with its more specific status.
        if (aRequireCompleteLifecycleCoverage &&
            !PartyQuestRuntimeLifecycleIntegrationPolicy::
                HasCompleteCharacterIdentityCoverage())
        {
            result.Status =
                PartyQuestRuntimeSessionBootstrapStatus::LifecycleCoverageIncomplete;
            return result;
        }

        // Keep generationLease alive through the complete synchronous process
        // bind. Lifecycle invalidation requires the exclusive side of the same
        // fence, so no character/runtime transition can cross publication.
        // BindVerifiedProcessOwner is private: the verified profile bootstrap is
        // structurally required for production access to the shared owner.
        auto& owner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
        result.Owner = owner.BindVerifiedProcessOwner(
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
