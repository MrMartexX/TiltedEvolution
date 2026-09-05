#include <Structs/Skyrim/PartyQuestRuntimeSessionBootstrap.h>

#include <Structs/Skyrim/PartyQuestCoopSaveLayout.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeLifecycleIntegration.h>
#include <Structs/Skyrim/PartyQuestRuntimeOwner.h>

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

        if (aRequireCompleteLifecycleCoverage &&
            !PartyQuestRuntimeLifecycleIntegrationPolicy::
                HasCompleteCharacterIdentityCoverage())
        {
            result.Status =
                PartyQuestRuntimeSessionBootstrapStatus::LifecycleCoverageIncomplete;
            return result;
        }

        // Keep generationLease alive through the complete synchronous process
        // bind. All identity-changing engine boundaries need the exclusive side
        // of this same fence and therefore cannot cross durable publication.
        auto& runtimeOwner = PartyQuestRuntimeOwner::GetProcessOwner();
        result.Owner = runtimeOwner.GetSessionOwner().BindVerifiedProcessOwner(
            acCampaignId,
            acPlayerProfile.GetProfileId(),
            *paths);
        if (!result.Owner.IsReadyForAdmission())
        {
            result.Status = result.Owner.IsBound()
                ? PartyQuestRuntimeSessionBootstrapStatus::OwnerRecoveryBlocked
                : PartyQuestRuntimeSessionBootstrapStatus::OwnerRejected;
            return result;
        }

        if (!runtimeOwner.PublishRuntimeSessionBound(
                generationLease->GetGeneration(),
                acCampaignId,
                acPlayerProfile.GetProfileId(),
                result.Owner))
        {
            result.Status =
                PartyQuestRuntimeSessionBootstrapStatus::RuntimeGenerationUnavailable;
            return result;
        }

        result.Status = PartyQuestRuntimeSessionBootstrapStatus::Bound;
        return result;
    }
    catch (...)
    {
        result.Status = PartyQuestRuntimeSessionBootstrapStatus::InvalidLayout;
        return result;
    }
}
