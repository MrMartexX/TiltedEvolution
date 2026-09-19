#include <Structs/Skyrim/PartyQuestAsyncSaveEventRouter.h>

#include <utility>

namespace
{
PartyQuestAsyncSaveEventRouteStatus MapAdapterFailure(
    PartyQuestNativeSaveEventAdapterStatus aStatus) noexcept
{
    switch (aStatus)
    {
    case PartyQuestNativeSaveEventAdapterStatus::Malformed:
        return PartyQuestAsyncSaveEventRouteStatus::Malformed;
    case PartyQuestNativeSaveEventAdapterStatus::InvalidExpectedIdentity:
        return PartyQuestAsyncSaveEventRouteStatus::InvalidExpectedIdentity;
    case PartyQuestNativeSaveEventAdapterStatus::IdentityMismatch:
        return PartyQuestAsyncSaveEventRouteStatus::IdentityMismatch;
    default:
        return PartyQuestAsyncSaveEventRouteStatus::AdapterRejected;
    }
}
}

PartyQuestAsyncSaveEventRouteResult
PartyQuestAsyncSaveEventRouter::ApplyArtifact(
    const void* apPayload,
    size_t aPayloadSize,
    const PartyQuestAsyncSaveRequestIdentity& acReservedIdentity,
    PartyQuestAsyncSaveContract& aContract,
    PartyQuestAsyncSaveFinalizationGate& aGate,
    uint64_t aNowMs) noexcept
{
    PartyQuestAsyncSaveEventRouteResult result;
    auto adapted = PartyQuestNativeSaveEventAdapter::ApplyTrustedProviderEvent(
        apPayload,
        aPayloadSize,
        acReservedIdentity,
        aContract,
        aNowMs);
    result.DecodeStatus = adapted.DecodeStatus;
    if (!adapted.ContractResult)
    {
        result.Status = MapAdapterFailure(adapted.Status);
        return result;
    }

    result.Finalization = aGate.ObserveContractResult(
        acReservedIdentity, std::move(*adapted.ContractResult));
    result.Status = PartyQuestAsyncSaveEventRouteStatus::Applied;
    return result;
}

PartyQuestAsyncSaveEventRouteResult
PartyQuestAsyncSaveEventRouter::ApplyRetirement(
    const void* apPayload,
    size_t aPayloadSize,
    const PartyQuestAsyncSaveRequestIdentity& acReservedIdentity,
    PartyQuestAsyncSaveContract& aContract,
    PartyQuestAsyncSaveFinalizationGate& aGate) noexcept
{
    PartyQuestAsyncSaveEventRouteResult result;
    auto decoded = PartyQuestNativeSaveEventAdapter::DecodeRequestRetired(
        apPayload, aPayloadSize);
    result.DecodeStatus = decoded.Status;
    if (decoded.Status != PartyQuestNativeSaveEventDecodeStatus::Decoded ||
        !decoded.Event)
    {
        result.Status = PartyQuestAsyncSaveEventRouteStatus::Malformed;
        return result;
    }
    if (!acReservedIdentity.IsValid())
    {
        result.Status = PartyQuestAsyncSaveEventRouteStatus::InvalidExpectedIdentity;
        return result;
    }
    if (decoded.Event->Identity != acReservedIdentity)
    {
        result.Status = PartyQuestAsyncSaveEventRouteStatus::IdentityMismatch;
        return result;
    }

    const auto outcome = decoded.Event->Outcome ==
            PartyQuestNativeSaveEventOutcome::Succeeded ?
        PartyQuestAsyncSaveFinalOutcome::Succeeded :
        PartyQuestAsyncSaveFinalOutcome::Failed;
    result.Finalization = aGate.ObserveRetirement(
        aContract, acReservedIdentity, outcome);
    result.Status = PartyQuestAsyncSaveEventRouteStatus::Applied;
    return result;
}
