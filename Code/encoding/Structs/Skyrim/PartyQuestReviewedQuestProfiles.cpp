#include <Structs/Skyrim/PartyQuestReviewedQuestProfiles.h>

#include <array>

namespace
{
const std::array<PartyQuestRuntimeCompatibilityRequirement, 2> kReviewedProfiles{
    {{GameId(PartyQuestReviewedQuestProfiles::NorthwatchModId, PartyQuestReviewedQuestProfiles::NorthwatchBaseId), PartyQuestReviewedQuestProfiles::ProfileVersion,
      PartyQuestReviewedQuestProfiles::NorthwatchResolvedRecordFingerprint, PartyQuestReviewedQuestProfiles::NorthwatchWinningOverrideFingerprint,
      PartyQuestReviewedQuestProfiles::ReferenceScriptEnvironment, PartyQuestReviewedQuestProfiles::NativeAdapterFingerprint, PartyQuestVerificationComponent::QuestSnapshot},
     {GameId(PartyQuestReviewedQuestProfiles::KarthspireModId, PartyQuestReviewedQuestProfiles::KarthspireBaseId), PartyQuestReviewedQuestProfiles::ProfileVersion,
      PartyQuestReviewedQuestProfiles::KarthspireResolvedRecordFingerprint, PartyQuestReviewedQuestProfiles::KarthspireWinningOverrideFingerprint,
      PartyQuestReviewedQuestProfiles::ReferenceScriptEnvironment, PartyQuestReviewedQuestProfiles::NativeAdapterFingerprint, PartyQuestVerificationComponent::QuestSnapshot}}};

const std::array<PartyQuestReviewedTransition, 2> kReviewedTransitions{
    {{{GameId(PartyQuestReviewedQuestProfiles::NorthwatchModId, PartyQuestReviewedQuestProfiles::NorthwatchBaseId), PartyQuestReviewedQuestProfiles::NorthwatchSourceStage,
       PartyQuestReviewedQuestProfiles::NorthwatchTargetStage, 1, PartyQuestReviewedQuestProfiles::ReferencePluginEnvironment,
       PartyQuestReviewedQuestProfiles::NorthwatchAnalyzerTopologyFingerprint, PartyQuestReviewedQuestProfiles::NorthwatchAnalyzerOverrideFingerprint,
       PartyQuestReviewedQuestProfiles::ReferenceScriptEnvironment},
      1,
      1,
      PartyQuestReviewedQuestProfiles::ReferenceAnalyzerProvenance,
      0x5245564E57415443ull, // "REVNWATC"
      PartyQuestExpectedRunningState::Running,
      {PartyQuestReviewedQuestProfiles::NorthwatchSourceStage},
      {},
      false,
      false,
      false,
      false,
      PartyQuestRecoveryClass::NoMutation},
     {{GameId(PartyQuestReviewedQuestProfiles::KarthspireModId, PartyQuestReviewedQuestProfiles::KarthspireBaseId), PartyQuestReviewedQuestProfiles::KarthspireSourceStage,
       PartyQuestReviewedQuestProfiles::KarthspireTargetStage, 1, PartyQuestReviewedQuestProfiles::ReferencePluginEnvironment,
       PartyQuestReviewedQuestProfiles::KarthspireAnalyzerTopologyFingerprint, PartyQuestReviewedQuestProfiles::KarthspireAnalyzerOverrideFingerprint,
       PartyQuestReviewedQuestProfiles::ReferenceScriptEnvironment},
      1,
      1,
      PartyQuestReviewedQuestProfiles::ReferenceAnalyzerProvenance,
      0x5245564B53504952ull, // "REVKSPIR"
      PartyQuestExpectedRunningState::Running,
      {PartyQuestReviewedQuestProfiles::KarthspireSourceStage},
      {},
      false,
      false,
      false,
      false,
      PartyQuestRecoveryClass::NoMutation}}};
} // namespace

PartyQuestRuntimeCompatibilityManifest PartyQuestReviewedQuestProfiles::BuildReviewedManifest() noexcept
{
    PartyQuestRuntimeCompatibilityManifest manifest;
    for (const auto& profile : kReviewedProfiles)
    {
        if (!PartyQuestRuntimeCompatibilityPolicy::IsValidRequirement(profile) || !manifest.AddRequirement(profile))
        {
            return {};
        }
    }
    return manifest;
}

bool PartyQuestReviewedQuestProfiles::HasReviewedProfile(const GameId& acQuestId) noexcept
{
    for (const auto& profile : kReviewedProfiles)
    {
        if (profile.QuestId == acQuestId)
            return true;
    }
    return false;
}

const PartyQuestRuntimeCompatibilityRequirement* PartyQuestReviewedQuestProfiles::FindRequirement(const GameId& acQuestId) noexcept
{
    for (const auto& profile : kReviewedProfiles)
    {
        if (profile.QuestId == acQuestId)
            return &profile;
    }
    return nullptr;
}

PartyQuestReviewedTransitionManifest PartyQuestReviewedQuestProfiles::BuildReviewedTransitionManifest() noexcept
{
    PartyQuestReviewedTransitionManifest manifest;
    if (!manifest.Publish(kReviewedTransitions))
    {
        return {};
    }
    return manifest;
}

const PartyQuestReviewedTransition* PartyQuestReviewedQuestProfiles::FindReviewedTransition(const PartyQuestTransitionIdentity& acIdentity) noexcept
{
    for (const auto& transition : kReviewedTransitions)
    {
        if (transition.Identity == acIdentity)
            return &transition;
    }
    return nullptr;
}

const PartyQuestReviewedTransition* PartyQuestReviewedQuestProfiles::FindReviewedTransition(const GameId& acQuestId, uint16_t aSourceStage, uint16_t aTargetStage) noexcept
{
    for (const auto& transition : kReviewedTransitions)
    {
        if (transition.Identity.QuestId == acQuestId && transition.Identity.SourceStage == aSourceStage && transition.Identity.TargetStage == aTargetStage)
        {
            return &transition;
        }
    }
    return nullptr;
}

std::span<const PartyQuestRuntimeCompatibilityRequirement> PartyQuestReviewedQuestProfiles::GetReviewedRequirements() noexcept
{
    return kReviewedProfiles;
}

std::span<const PartyQuestReviewedTransition> PartyQuestReviewedQuestProfiles::GetReviewedTransitions() noexcept
{
    return kReviewedTransitions;
}
