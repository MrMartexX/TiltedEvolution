#include <TiltedOnlinePCH.h>

#include <Services/QuestSnapshotCollector.h>

#include <Systems/ModSystem.h>

#include <Forms/BGSBaseAlias.h>
#include <Forms/TESQuest.h>
#include <TESObjectREFR.h>

namespace
{
QuestSnapshotStatus GetQuestStatus(const TESQuest& acQuest) noexcept
{
    if ((acQuest.flags & TESQuest::Flags::Failed) != 0)
        return QuestSnapshotStatus::Failed;

    if ((acQuest.flags & TESQuest::Flags::Completed) != 0)
        return QuestSnapshotStatus::Completed;

    if (!acQuest.IsStopped())
        return QuestSnapshotStatus::Running;

    return acQuest.alreadyRun ? QuestSnapshotStatus::Stopped : QuestSnapshotStatus::Inactive;
}

QuestObjectiveState GetObjectiveState(uint8_t aState) noexcept
{
    // Skyrim objective states: dormant, displayed, completed,
    // completed+displayed, failed, failed+displayed.
    switch (aState)
    {
    case 1: return QuestObjectiveState::Displayed;
    case 2:
    case 3: return QuestObjectiveState::Completed;
    case 4:
    case 5: return QuestObjectiveState::Failed;
    default: return QuestObjectiveState::Hidden;
    }
}

const char* GetStatusName(QuestSnapshotStatus aStatus) noexcept
{
    switch (aStatus)
    {
    case QuestSnapshotStatus::Inactive: return "inactive";
    case QuestSnapshotStatus::Running: return "running";
    case QuestSnapshotStatus::Stopped: return "stopped";
    case QuestSnapshotStatus::Completed: return "completed";
    case QuestSnapshotStatus::Failed: return "failed";
    default: return "unknown";
    }
}

const char* GetSyncClassName(PartyQuestSyncClass aClass) noexcept
{
    switch (aClass)
    {
    case PartyQuestSyncClass::SharedCandidate: return "shared-candidate";
    case PartyQuestSyncClass::ServiceCandidate: return "service-candidate";
    case PartyQuestSyncClass::LocalOnly: return "local-only";
    default: return "unknown";
    }
}

const char* GetSyncReasonName(PartyQuestSyncReason aReason) noexcept
{
    switch (aReason)
    {
    case PartyQuestSyncReason::GameplayQuestType: return "gameplay-type";
    case PartyQuestSyncReason::UserFacingMiscellaneous: return "user-facing-misc";
    case PartyQuestSyncReason::UserFacingUntyped: return "user-facing-untyped";
    case PartyQuestSyncReason::HiddenMiscellaneous: return "hidden-misc";
    case PartyQuestSyncReason::HiddenUntyped: return "hidden-untyped";
    case PartyQuestSyncReason::NoStages: return "no-stages";
    default: return "unknown";
    }
}

bool IsAliasType(const BGSBaseAlias& acAlias, const char* acType) noexcept
{
    const char* pType = acAlias.QType().AsAscii();
    return pType && std::strcmp(pType, acType) == 0;
}

std::optional<GameId> GetServerGameId(uint32_t aFormId, const ModSystem& acModSystem) noexcept
{
    GameId id;
    if (!acModSystem.GetServerModId(aFormId, id))
        return std::nullopt;

    return id;
}
} // namespace

std::optional<QuestSnapshot> QuestSnapshotCollector::Collect(TESQuest* apQuest, const ModSystem& acModSystem) noexcept
{
    if (!apQuest)
        return std::nullopt;

    const auto questId = GetServerGameId(apQuest->formID, acModSystem);
    if (!questId)
    {
        spdlog::warn("QuestSnapshot: failed to map quest form {:08X} to a server GameId", apQuest->formID);
        return std::nullopt;
    }

    QuestSnapshot snapshot;
    snapshot.QuestId = *questId;
    snapshot.Status = GetQuestStatus(*apQuest);
    snapshot.CurrentStage = apQuest->currentStage;

    for (TESQuest::Stage* pStage : apQuest->stages)
    {
        if (pStage && pStage->IsDone())
            snapshot.CompletedStages.push_back(pStage->stageIndex);
    }

    for (TESQuest::Objective* pObjective : apQuest->objectives)
    {
        if (!pObjective)
            continue;

        snapshot.Objectives.push_back({pObjective->stageId, GetObjectiveState(pObjective->state)});
    }

    for (BGSBaseAlias* pAlias : apQuest->aliases)
    {
        if (!pAlias)
            continue;

        if (IsAliasType(*pAlias, "Ref"))
        {
            std::optional<GameId> referenceId;
            if (TESObjectREFR* pReference = apQuest->GetAliasedRef(pAlias->aliasId))
            {
                referenceId = GetServerGameId(pReference->formID, acModSystem);
                if (!referenceId)
                {
                    // Dynamic references need a party-owned runtime ID before they
                    // can be canonical across clients. Keep the alias present but
                    // unresolved in this read-only PoC.
                    spdlog::debug("QuestSnapshot: alias {} resolved to unmapped local reference {:08X}", pAlias->aliasId, pReference->formID);
                }
            }

            snapshot.ReferenceAliases.push_back({pAlias->aliasId, referenceId, pAlias->IsQuestObject()});

            if (pAlias->fillType == BGSBaseAlias::FillType::Created && referenceId)
                snapshot.CreatedReferences.push_back(*referenceId);
        }
        else if (IsAliasType(*pAlias, "Loc"))
        {
            // The alias identity is collected now. Resolving and restoring the
            // selected BGSLocation is the dedicated location-alias PoC because
            // TESQuest does not currently expose a safe location accessor.
            snapshot.LocationAliases.push_back({pAlias->aliasId, std::nullopt});
        }
        else
        {
            const char* pType = pAlias->QType().AsAscii();
            spdlog::debug("QuestSnapshot: unsupported alias type '{}' for alias {}", pType ? pType : "", pAlias->aliasId);
        }
    }

    snapshot.Canonicalize();
    return snapshot;
}

PartyQuestSyncFacts QuestSnapshotCollector::CollectSyncFacts(TESQuest* apQuest) noexcept
{
    PartyQuestSyncFacts facts;
    if (!apQuest)
        return facts;

    const char* pDisplayName = apQuest->fullName.value.AsAscii();
    facts.QuestType = static_cast<uint8_t>(apQuest->type);
    facts.HasStages = !apQuest->stages.Empty();
    facts.IsDisplayedInHud = (apQuest->flags & TESQuest::Flags::DisplayedInHUD) != 0;
    facts.HasDisplayName = pDisplayName && *pDisplayName != '\0';
    return facts;
}

PartyQuestSyncClassification QuestSnapshotCollector::Classify(TESQuest* apQuest) noexcept
{
    return ClassifyPartyQuestSync(CollectSyncFacts(apQuest));
}

void QuestSnapshotCollector::Log(TESQuest* apQuest, const QuestSnapshot& acSnapshot, const char* acReason) noexcept
{
    if (!apQuest)
        return;

    const PartyQuestSyncClassification classification = Classify(apQuest);
    spdlog::info(
        "QuestSnapshot[{}]: form={:08X} gameId={:016X} editorId='{}' status={} stage={} digest={:016X} completedStages={} objectives={} refAliases={} locAliases={} createdRefs={} syncClass={} syncReason={} questType={}",
        acReason ? acReason : "unknown", apQuest->formID, acSnapshot.QuestId.LogFormat(), apQuest->idName.AsAscii(), GetStatusName(acSnapshot.Status),
        acSnapshot.CurrentStage, acSnapshot.ComputeDigest(), acSnapshot.CompletedStages.size(), acSnapshot.Objectives.size(),
        acSnapshot.ReferenceAliases.size(), acSnapshot.LocationAliases.size(), acSnapshot.CreatedReferences.size(),
        GetSyncClassName(classification.Class), GetSyncReasonName(classification.Reason), static_cast<uint8_t>(apQuest->type));

    // Classification is enforced before the diagnostic transaction is sent.
    // These details remain observational and never mutate the local quest.
    for (const auto& objective : acSnapshot.Objectives)
        spdlog::info("QuestSnapshotDetail objective: index={} state={}", objective.Index, static_cast<uint8_t>(objective.State));

    for (const auto& alias : acSnapshot.ReferenceAliases)
    {
        if (alias.ReferenceId)
            spdlog::info("QuestSnapshotDetail refAlias: id={} ref={:016X} questObject={}", alias.AliasId, alias.ReferenceId->LogFormat(), alias.IsQuestObject);
        else
            spdlog::info("QuestSnapshotDetail refAlias: id={} ref=<unfilled-or-unmapped> questObject={}", alias.AliasId, alias.IsQuestObject);
    }

    for (const auto& alias : acSnapshot.LocationAliases)
        spdlog::info("QuestSnapshotDetail locationAlias: id={} location=<pending-location-accessor-poc>", alias.AliasId);
}
