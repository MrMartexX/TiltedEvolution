#include <TiltedOnlinePCH.h>

#include <Services/PartyQuestRuntimeOwnerService.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/PartyJoinedEvent.h>
#include <Events/PartyLeftEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/PartyQuestMessages.h>
#include <PartyQuestSkyrimPapyrusRuntimeObserver.h>
#include <PartyQuestSkyrimStageMutationExecutor.h>
#include <PlayerCharacter.h>
#include <Services/QuestService.h>
#include <Structs/Skyrim/PartyQuestExternalSinkLifetime.h>
#include <Structs/Skyrim/PartyQuestPlayerProfileLineage.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeOwner.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionBootstrap.h>
#include <World.h>

#include <ShlObj.h>

namespace
{
std::filesystem::path ResolveCoopReplicaRoot() noexcept
{
    wchar_t documents[MAX_PATH]{};
    const HRESULT status = SHGetFolderPathW(
        nullptr,
        CSIDL_PERSONAL | CSIDL_FLAG_CREATE,
        nullptr,
        SHGFP_TYPE_CURRENT,
        documents);
    if (FAILED(status) || documents[0] == L'\0')
        return {};

    try
    {
        return std::filesystem::path(documents) /
            L"My Games" /
            L"Skyrim Special Edition" /
            L"CoopCampaigns";
    }
    catch (...)
    {
        return {};
    }
}

void LogLifecycleFailure(
    const char* acBoundary,
    const PartyQuestRuntimeLifecycleFenceResult& acResult) noexcept
{
    if (acResult.CanProceed())
        return;

    spdlog::error(
        "PartyQuestRuntimeOwner retained durable recovery state at {}: status={} transaction={} guardHeld={}",
        acBoundary,
        static_cast<uint32_t>(acResult.Status),
        acResult.TransactionId,
        acResult.GuardHeld);
}
} // namespace

PartyQuestRuntimeOwnerService::PartyQuestRuntimeOwnerService(
    World& aWorld,
    entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    auto& owner = PartyQuestRuntimeOwner::GetProcessOwner();
    owner.ConfigureRuntimeAdapters(
        []() noexcept
        {
            auto* pPlayer = PlayerCharacter::Get();
            return pPlayer && pPlayer->GetNiNode();
        },
        [](uint64_t aTransactionId) noexcept
        {
            return PartyQuestSkyrimPapyrusRuntimeObserver::GetProcessObserver()
                .Observe(aTransactionId);
        },
        [](const PartyQuestRuntimeApplyRequest& acRequest) noexcept
        {
            return PartyQuestSkyrimStageMutationExecutor::Execute(
                acRequest,
                World::Get().GetModSystem());
        });

    m_connectedConnection = aDispatcher.sink<ConnectedEvent>()
        .connect<&PartyQuestRuntimeOwnerService::OnConnected>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>()
        .connect<&PartyQuestRuntimeOwnerService::OnDisconnected>(this);
    m_partyJoinedConnection = aDispatcher.sink<PartyJoinedEvent>()
        .connect<&PartyQuestRuntimeOwnerService::OnPartyJoined>(this);
    m_partyLeftConnection = aDispatcher.sink<PartyLeftEvent>()
        .connect<&PartyQuestRuntimeOwnerService::OnPartyLeft>(this);
    m_partyQuestRepairPlanConnection = aDispatcher.sink<NotifyPartyQuestRepairPlan>()
        .connect<&PartyQuestRuntimeOwnerService::OnPartyQuestRepairPlan>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>()
        .connect<&PartyQuestRuntimeOwnerService::OnUpdate>(this);

    // Persisted lineage is created only by the SKSE co-save load path. Observe
    // the engine's completed LoadGame edge as a retry trigger, never as identity
    // authority: TryBootstrap still requires the resolver's stable persisted
    // double-snapshot under the current generation lease.
    auto* pEventList = EventDispatcherManager::Get();
    if (pEventList)
    {
        m_pLoadGameDispatcher = &pEventList->loadGameEvent;
        m_pLoadGameDispatcher->RegisterSink(this);
    }
}

PartyQuestRuntimeOwnerService::~PartyQuestRuntimeOwnerService() noexcept
{
    PartyQuestReleaseExternalSink(
        m_pLoadGameDispatcher,
        static_cast<BSTEventSink<TESLoadGameEvent>*>(this));
    m_bootstrapSignal.Reset();

    auto& owner = PartyQuestRuntimeOwner::GetProcessOwner();
    if (!owner.IsShutdown())
    {
        const auto lifecycle = owner.GetSessionOwner().PrepareAndRelease(
            PartyQuestRuntimeLifecycleEvent::Disconnect);
        LogLifecycleFailure("owner-service-destroy", lifecycle);
    }
    owner.ClearRuntimeAdapters();
}

void PartyQuestRuntimeOwnerService::OnConnected(const ConnectedEvent&) noexcept
{
    auto& owner = PartyQuestRuntimeOwner::GetProcessOwner();
    const auto status = owner.ApplyClientBoundary(
        PartyQuestRuntimeOwner::ClientBoundary::Connected);
    if (status == PartyQuestRuntimeOwner::BoundaryStatus::SynchronizationFailed)
        spdlog::error("PartyQuestRuntimeOwner failed closed at connect generation boundary");

    // A connection boundary invalidates every previously latched retry. The
    // accepted repair plan for this connection will publish a fresh signal.
    m_bootstrapSignal.Reset();
}

void PartyQuestRuntimeOwnerService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_bootstrapSignal.Reset();

    auto& owner = PartyQuestRuntimeOwner::GetProcessOwner();
    const auto lifecycle = owner.GetSessionOwner().PrepareAndRelease(
        PartyQuestRuntimeLifecycleEvent::Disconnect);
    LogLifecycleFailure("disconnect", lifecycle);
}

void PartyQuestRuntimeOwnerService::OnPartyJoined(const PartyJoinedEvent&) noexcept
{
    auto& owner = PartyQuestRuntimeOwner::GetProcessOwner();
    const auto status = owner.ApplyClientBoundary(
        PartyQuestRuntimeOwner::ClientBoundary::PartyJoined);
    if (status == PartyQuestRuntimeOwner::BoundaryStatus::SynchronizationFailed)
        spdlog::error("PartyQuestRuntimeOwner failed closed at party-join generation boundary");

    // Party join itself is not campaign authorization. Wait for the server
    // repair-plan edge that QuestService validates into verified campaign state.
    m_bootstrapSignal.Reset();
}

void PartyQuestRuntimeOwnerService::OnPartyLeft(const PartyLeftEvent&) noexcept
{
    m_bootstrapSignal.Reset();

    auto& owner = PartyQuestRuntimeOwner::GetProcessOwner();
    const auto lifecycle = owner.GetSessionOwner().PrepareAndRelease(
        PartyQuestRuntimeLifecycleEvent::PartyLeave);
    LogLifecycleFailure("party-leave", lifecycle);
}

void PartyQuestRuntimeOwnerService::OnPartyQuestRepairPlan(
    const NotifyPartyQuestRepairPlan& acPlan) noexcept
{
    // The server repair plan is only an edge. QuestService remains the sole
    // owner of protocol/campaign admission and may reject it; the deferred
    // game-thread attempt below queries only GetVerifiedPartyQuestCampaignId().
    // Restrict the wakeup to structurally authoritative candidates so malformed
    // packets cannot create a retry loop.
    if (acPlan.IsValid && acPlan.CampaignId.IsValid() &&
        acPlan.ReportId != 0 && acPlan.PlanId != 0)
    {
        m_bootstrapSignal.Publish();
    }
}

BSTEventResult PartyQuestRuntimeOwnerService::OnEvent(
    const TESLoadGameEvent*,
    const EventDispatcher<TESLoadGameEvent>*)
{
    // This event means the character-load lifecycle produced new evidence that
    // may include a freshly persisted SKSE lineage. It grants no authority by
    // itself; the resolver revalidates the bridge and generation on consumption.
    m_bootstrapSignal.Publish();
    return BSTEventResult::kOk;
}

void PartyQuestRuntimeOwnerService::OnUpdate(const UpdateEvent&) noexcept
{
    auto& owner = PartyQuestRuntimeOwner::GetProcessOwner();
    if (owner.IsShutdown())
        return;

    // Update is only the thread-affinity consumer for already-published edges.
    // No bridge/campaign state is scanned unless an authoritative producer fired.
    if (m_bootstrapSignal.Consume())
        TryBootstrap();

    // Deferred runtime work is executed only from the client update thread. The
    // owner itself supplies the generation execution lease across validation and
    // callback entry; a lifecycle boundary therefore blocks before Skyrim access.
    (void)owner.ExecuteNext();
}

void PartyQuestRuntimeOwnerService::TryBootstrap() noexcept
{
    auto& owner = PartyQuestRuntimeOwner::GetProcessOwner();
    if (owner.IsShutdown() || !m_world.GetPartyService().IsInParty())
        return;

    const auto campaign = m_world.ctx().at<QuestService>()
        .GetVerifiedPartyQuestCampaignId();
    if (!campaign || !campaign->IsValid())
        return;

    auto& sessionOwner = owner.GetSessionOwner();
    if (sessionOwner.IsBound())
    {
        const auto* pSession = sessionOwner.GetRuntimeSession();
        if (!pSession || pSession->GetCampaignId() != *campaign)
        {
            const auto switched = sessionOwner.PrepareAndRelease(
                PartyQuestRuntimeLifecycleEvent::CampaignSwitch);
            if (!switched.CanProceed())
            {
                LogLifecycleFailure("campaign-switch-bootstrap", switched);
                return;
            }
        }
        // An identical campaign is deliberately not a shortcut. Duplicate bind
        // is idempotent in PartyQuestRuntimeSessionOwner, but rebinding aggregate
        // admission still requires a freshly resolved lineage authorization for
        // the current runtime generation.
    }

    const auto lineage = PartyQuestSkyrimPlayerProfileLineageResolver::Resolve();
    if (!lineage.IsVerified())
        return;

    const auto root = ResolveCoopReplicaRoot();
    if (root.empty() || !root.is_absolute() || root.filename() != L"CoopCampaigns")
    {
        spdlog::error("PartyQuestRuntimeOwner could not resolve an absolute co-op replica root");
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec)
    {
        spdlog::error(
            "PartyQuestRuntimeOwner could not create co-op replica root: {}",
            ec.message());
        return;
    }

    PartyQuestRuntimeSessionBootstrapResult bootstrap;
    const auto aggregate = owner.RunBootstrap(
        lineage.GetRuntimeGeneration(),
        [&]()
        {
            bootstrap = PartyQuestRuntimeSessionBootstrap::BindProcessOwner(
                root,
                *campaign,
                lineage);
            return bootstrap.IsBound();
        });

    if (aggregate.Status == PartyQuestRuntimeOwner::BootstrapStatus::Bound)
    {
        spdlog::info(
            "PartyQuestRuntimeOwner production bootstrap bound: campaign={:016X}{:016X} profile={:016X}{:016X} generation={}",
            campaign->High,
            campaign->Low,
            lineage.GetProfileId().High,
            lineage.GetProfileId().Low,
            aggregate.RuntimeGeneration);
        return;
    }

    if (aggregate.Status == PartyQuestRuntimeOwner::BootstrapStatus::Exception)
    {
        spdlog::error("PartyQuestRuntimeOwner bootstrap callback threw; admission remains closed");
        return;
    }

    if (bootstrap.Status != PartyQuestRuntimeSessionBootstrapStatus::UnverifiedPlayerProfile)
    {
        spdlog::debug(
            "PartyQuestRuntimeOwner bootstrap rejected fail-closed: aggregateStatus={} bootstrapStatus={} ownerStatus={} generation={}",
            static_cast<uint32_t>(aggregate.Status),
            static_cast<uint32_t>(bootstrap.Status),
            static_cast<uint32_t>(bootstrap.Owner.Status),
            aggregate.RuntimeGeneration);
    }
}
