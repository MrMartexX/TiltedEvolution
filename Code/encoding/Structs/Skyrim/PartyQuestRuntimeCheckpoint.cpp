#include <Structs/Skyrim/PartyQuestRuntimeCheckpoint.h>

#include <array>
#include <cstring>
#include <string>
#include <type_traits>

namespace
{
constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void HashBytes(uint64_t& aHash, const void* apData, size_t aSize) noexcept
{
    const auto* bytes = static_cast<const uint8_t*>(apData);
    for (size_t i = 0; i < aSize; ++i)
    {
        aHash ^= bytes[i];
        aHash *= kFnvPrime;
    }
}

template <class T>
void HashValue(uint64_t& aHash, const T& acValue) noexcept
{
    static_assert(std::is_trivially_copyable_v<T>);
    HashBytes(aHash, &acValue, sizeof(T));
}

void HashString(uint64_t& aHash, const std::string& acValue) noexcept
{
    const uint64_t size = static_cast<uint64_t>(acValue.size());
    HashValue(aHash, size);
    if (!acValue.empty())
        HashBytes(aHash, acValue.data(), acValue.size());
}

PartyQuestRuntimeCheckpointResult MakeResult(
    PartyQuestRuntimeCheckpointStatus aStatus,
    const PartyQuestRuntimeApplyEntry* apActive,
    const std::filesystem::path& acManifestPath = {})
{
    PartyQuestRuntimeCheckpointResult result;
    result.Status = aStatus;
    result.ManifestPath = acManifestPath;
    if (apActive)
    {
        result.TransactionId = apActive->TransactionId;
        result.TargetWorldRevision = apActive->TargetWorldRevision;
    }
    return result;
}

bool IsCheckpointReadyState(const PartyQuestRuntimeApplyEntry& acActive) noexcept
{
    return acActive.State == PartyQuestRuntimeApplyState::ReadyToApply &&
        acActive.SaveGuardActive &&
        acActive.CheckpointCreated &&
        !acActive.RuntimeMutationMayHaveOccurred;
}

bool IsAwaitingCheckpointState(const PartyQuestRuntimeApplyEntry& acActive) noexcept
{
    return acActive.State == PartyQuestRuntimeApplyState::AwaitingCheckpoint &&
        acActive.SaveGuardActive &&
        !acActive.CheckpointCreated &&
        !acActive.RuntimeMutationMayHaveOccurred;
}
} // namespace

uint64_t PartyQuestRuntimeCheckpointCoverageAuthorization::ComputePlanFingerprint(
    const PartyQuestReplicaCopyPlan& acPlan) noexcept
{
    try
    {
        if (!acPlan.IsReady() || acPlan.Operations.empty())
            return 0;

        uint64_t hash = kFnvOffset;
        const auto status = static_cast<uint8_t>(acPlan.Status);
        HashValue(hash, status);

        const uint64_t count = static_cast<uint64_t>(acPlan.Operations.size());
        HashValue(hash, count);
        for (const auto& operation : acPlan.Operations)
        {
            const auto kind = static_cast<uint8_t>(operation.Kind);
            HashValue(hash, kind);
            HashString(hash, operation.SourcePath.lexically_normal().generic_string());
            HashString(hash, operation.DestinationPath.lexically_normal().generic_string());
            HashValue(hash, operation.ExpectedSize);
            HashValue(hash, operation.ExpectedDigest);
        }

        return hash != 0 ? hash : 1;
    }
    catch (...)
    {
        return 0;
    }
}

PartyQuestRuntimeCheckpointCoverageAuthorization::
PartyQuestRuntimeCheckpointCoverageAuthorization(
    uint64_t aTransactionId,
    uint64_t aTargetWorldRevision,
    const PartyQuestReplicaCopyPlan& acPlan) noexcept
    : m_transactionId(aTransactionId)
    , m_targetWorldRevision(aTargetWorldRevision)
    , m_planFingerprint(ComputePlanFingerprint(acPlan))
    , m_operationCount(acPlan.Operations.size())
    , m_verified(
          aTransactionId != 0 &&
          aTargetWorldRevision != 0 &&
          acPlan.IsReady() &&
          !acPlan.Operations.empty() &&
          m_planFingerprint != 0)
{
}

bool PartyQuestRuntimeCheckpointCoverageAuthorization::Matches(
    uint64_t aTransactionId,
    uint64_t aTargetWorldRevision,
    const PartyQuestReplicaCopyPlan& acPlan) const noexcept
{
    if (!m_verified ||
        aTransactionId == 0 ||
        aTargetWorldRevision == 0 ||
        aTransactionId != m_transactionId ||
        aTargetWorldRevision != m_targetWorldRevision ||
        !acPlan.IsReady() ||
        acPlan.Operations.size() != m_operationCount)
    {
        return false;
    }

    const uint64_t fingerprint = ComputePlanFingerprint(acPlan);
    return fingerprint != 0 && fingerprint == m_planFingerprint;
}

PartyQuestRuntimeCheckpointResult
PartyQuestRuntimeCheckpointCoordinator::EnsurePreRepairCheckpoint(
    PartyQuestRuntimeApplySession& aSession,
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaCopyPlan& acCheckpointPlan,
    const PartyQuestRuntimeCheckpointCoverageAuthorization& acCoverage,
    const PartyQuestReplicaWorkspacePublicationCapability* apPublicationCapability) noexcept
{
    try
    {
        if (!aSession.GetCampaignId().IsValid() ||
            !aSession.GetPlayerProfileId().IsValid())
        {
            return MakeResult(
                PartyQuestRuntimeCheckpointStatus::InvalidIdentity,
                aSession.GetCoordinator().GetActive());
        }

        if (!PartyQuestCoopSaveLayout::Matches(
                acPaths,
                aSession.GetCampaignId(),
                aSession.GetPlayerProfileId()))
        {
            return MakeResult(
                PartyQuestRuntimeCheckpointStatus::InvalidLayout,
                aSession.GetCoordinator().GetActive());
        }

        const PartyQuestRuntimeApplyEntry* pActive =
            aSession.GetCoordinator().GetActive();
        if (!pActive ||
            pActive->TransactionId == 0 ||
            pActive->TargetWorldRevision == 0)
        {
            return MakeResult(
                PartyQuestRuntimeCheckpointStatus::InvalidRuntimeState,
                pActive);
        }

        const uint64_t transactionId = pActive->TransactionId;
        const uint64_t targetWorldRevision = pActive->TargetWorldRevision;
        const auto manifestPath =
            PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
                acPaths,
                PartyQuestCheckpointKind::PreRepair,
                targetWorldRevision);

        if (!acCoverage.Matches(
                transactionId,
                targetWorldRevision,
                acCheckpointPlan))
        {
            return MakeResult(
                PartyQuestRuntimeCheckpointStatus::InvalidCoverageAuthorization,
                pActive,
                manifestPath);
        }

        PartyQuestReplicaSnapshotManager manager(
            acPaths,
            aSession.GetCampaignId(),
            aSession.GetPlayerProfileId());

        if (IsCheckpointReadyState(*pActive))
        {
            PartyQuestRuntimeCheckpointResult result = MakeResult(
                PartyQuestRuntimeCheckpointStatus::AlreadyReady,
                pActive,
                manifestPath);
            const auto validation = manager.ValidateRevisionCheckpoint(
                PartyQuestCheckpointKind::PreRepair,
                targetWorldRevision);
            result.SnapshotStatus = validation.Status;
            if (!validation.IsReady())
                result.Status = PartyQuestRuntimeCheckpointStatus::SnapshotFailed;
            return result;
        }

        if (!IsAwaitingCheckpointState(*pActive))
        {
            return MakeResult(
                PartyQuestRuntimeCheckpointStatus::InvalidRuntimeState,
                pActive,
                manifestPath);
        }

        if (!acCheckpointPlan.IsReady())
        {
            return MakeResult(
                PartyQuestRuntimeCheckpointStatus::InvalidCheckpointPlan,
                pActive,
                manifestPath);
        }

        const auto snapshot = apPublicationCapability
            ? manager.EnsureRevisionCheckpoint(
                  PartyQuestCheckpointKind::PreRepair,
                  targetWorldRevision,
                  acCheckpointPlan,
                  *apPublicationCapability)
            : manager.EnsureRevisionCheckpoint(
                  PartyQuestCheckpointKind::PreRepair,
                  targetWorldRevision,
                  acCheckpointPlan);

        PartyQuestRuntimeCheckpointResult result = MakeResult(
            snapshot.Status == PartyQuestReplicaSnapshotStatus::AlreadyReady
                ? PartyQuestRuntimeCheckpointStatus::AlreadyReady
                : PartyQuestRuntimeCheckpointStatus::Ready,
            pActive,
            manifestPath);
        result.SnapshotStatus = snapshot.Status;

        if (!snapshot.IsReady())
        {
            result.Status = snapshot.Status == PartyQuestReplicaSnapshotStatus::InvalidPlan
                ? PartyQuestRuntimeCheckpointStatus::InvalidCheckpointPlan
                : PartyQuestRuntimeCheckpointStatus::SnapshotFailed;
            return result;
        }

        result.RuntimeTransition = aSession.MarkCheckpointCreated(transactionId);
        switch (result.RuntimeTransition)
        {
        case PartyQuestRuntimeDurableTransitionStatus::Applied:
            break;

        case PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure:
            result.Status = PartyQuestRuntimeCheckpointStatus::RuntimeStatePersistenceFailed;
            break;

        case PartyQuestRuntimeDurableTransitionStatus::InvalidState:
        case PartyQuestRuntimeDurableTransitionStatus::CheckpointRestoreRequired:
        case PartyQuestRuntimeDurableTransitionStatus::InsufficientDurability:
            result.Status = PartyQuestRuntimeCheckpointStatus::InvalidRuntimeState;
            break;
        }

        return result;
    }
    catch (...)
    {
        return MakeResult(
            PartyQuestRuntimeCheckpointStatus::SnapshotFailed,
            aSession.GetCoordinator().GetActive());
    }
}
