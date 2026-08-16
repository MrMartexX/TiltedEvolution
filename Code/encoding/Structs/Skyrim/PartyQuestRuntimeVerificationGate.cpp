#include <Structs/Skyrim/PartyQuestRuntimeVerificationGate.h>

#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>
#include <Structs/Skyrim/PartyQuestSaveGuard.h>

namespace
{
PartyQuestRuntimeGuardedVerificationResult RejectStructuralMismatch(
    PartyQuestRuntimeGuardedSession& aGuardedSession,
    PartyQuestRuntimeVerificationMonitor& aMonitor,
    uint64_t aTransactionId) noexcept
{
    PartyQuestRuntimeGuardedVerificationResult result;
    result.TransactionId = aTransactionId;
    result.MonitorStatus = aMonitor.GetStatus();

    const auto& guard = aGuardedSession.GetSaveGuard();
    result.GuardHeld = aTransactionId != 0 &&
        guard.IsActive() &&
        guard.GetTransactionId() == aTransactionId;
    return result;
}

PartyQuestRuntimeGuardedVerificationResult SubmitInvalidVerification(
    PartyQuestRuntimeGuardedSession& aGuardedSession,
    PartyQuestRuntimeVerificationMonitor& aMonitor,
    uint64_t aTransactionId,
    uint64_t aNowMs) noexcept
{
    QuestSnapshot invalid;
    return aGuardedSession.SubmitVerificationResnapshot(
        aMonitor,
        aTransactionId,
        aNowMs,
        std::move(invalid));
}
} // namespace

PartyQuestRuntimeGuardedVerificationResult PartyQuestRuntimeVerificationGate::Submit(
    PartyQuestRuntimeGuardedSession& aGuardedSession,
    PartyQuestRuntimeApplySession& aSession,
    PartyQuestRuntimeVerificationMonitor& aMonitor,
    uint64_t aTransactionId,
    uint64_t aNowMs,
    const PartyQuestRuntimeCompatibilityRequirement& acRequirement,
    const SnapshotObserver& acSnapshotObserver,
    const CompatibilityObserver& acCompatibilityObserver) noexcept
{
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    auto& processOwner = PartyQuestRuntimeSessionOwner::GetProcessOwner();

    // INV-LIFECYCLE-001 / INV-VERIFY-001: structural wiring must be proven
    // before any fail-closed verification submission is allowed to mutate
    // runtime/recovery state. A caller cannot pair the process-owned guarded
    // wrapper with another durable session, nor can a private guarded wrapper
    // drive verification for the process-owned durable session.
    if (!processOwner.IsBound() ||
        processOwner.GetGuardedSession() != &aGuardedSession ||
        processOwner.GetRuntimeSession() != &aSession ||
        &aGuardedSession.GetRuntimeSession() != &aSession ||
        &aGuardedSession.GetSaveGuard() != &processGuard)
    {
        return RejectStructuralMismatch(
            aGuardedSession,
            aMonitor,
            aTransactionId);
    }

    const auto* active = aSession.GetCoordinator().GetActive();
    if (!active ||
        aTransactionId == 0 ||
        active->TransactionId != aTransactionId ||
        active->State != PartyQuestRuntimeApplyState::Verifying ||
        !active->SaveGuardActive ||
        !active->CheckpointCreated ||
        !active->RuntimeMutationMayHaveOccurred ||
        !processGuard.IsActive() ||
        processGuard.GetTransactionId() != aTransactionId ||
        aMonitor.GetTransactionId() != aTransactionId ||
        aMonitor.GetStatus() != PartyQuestRuntimeVerificationMonitorStatus::Waiting ||
        acRequirement.QuestId != active->QuestId ||
        !PartyQuestRuntimeCompatibilityPolicy::IsValidRequirement(acRequirement) ||
        !acSnapshotObserver ||
        !acCompatibilityObserver)
    {
        return SubmitInvalidVerification(
            aGuardedSession,
            aMonitor,
            aTransactionId,
            aNowMs);
    }

    auto& generationFence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const uint64_t generation = generationFence.GetGeneration();
    auto lease = generationFence.TryAcquire(generation);
    if (!lease || !lease->IsValid())
    {
        return SubmitInvalidVerification(
            aGuardedSession,
            aMonitor,
            aTransactionId,
            aNowMs);
    }

    std::optional<QuestSnapshot> snapshot;
    std::optional<PartyQuestRuntimeCompatibilityFacts> facts;
    try
    {
        snapshot = acSnapshotObserver(active->QuestId);
        facts = acCompatibilityObserver(active->QuestId);
    }
    catch (...)
    {
        snapshot.reset();
        facts.reset();
    }

    if (!snapshot || !facts || generationFence.GetGeneration() != generation)
    {
        return SubmitInvalidVerification(
            aGuardedSession,
            aMonitor,
            aTransactionId,
            aNowMs);
    }

    snapshot->Canonicalize();
    if (snapshot->QuestId != active->QuestId)
    {
        return SubmitInvalidVerification(
            aGuardedSession,
            aMonitor,
            aTransactionId,
            aNowMs);
    }

    const auto compatibility =
        PartyQuestRuntimeCompatibilityPolicy::Evaluate(acRequirement, *facts);
    if (!compatibility.IsAuthorized() ||
        compatibility.SafetyProfile.GetCompatibilityFingerprint() !=
            active->ExpectedVerification.CompatibilityFingerprint ||
        compatibility.SafetyProfile.GetAdapterMutationComponents() !=
            PartyQuestVerificationComponent::QuestSnapshot)
    {
        return SubmitInvalidVerification(
            aGuardedSession,
            aMonitor,
            aTransactionId,
            aNowMs);
    }

    if (!aSession.PrepareVerificationCompatibilityInternal(
            aTransactionId,
            generation,
            compatibility.SafetyProfile))
    {
        return SubmitInvalidVerification(
            aGuardedSession,
            aMonitor,
            aTransactionId,
            aNowMs);
    }

    const auto result = aGuardedSession.SubmitVerificationResnapshot(
        aMonitor,
        aTransactionId,
        aNowMs,
        std::move(*snapshot));

    // Defensive cleanup if guarded verification rejected the call before the
    // exact process-owned session could consume its one-shot capability.
    aSession.m_pendingVerificationCompatibility.reset();
    return result;
}
