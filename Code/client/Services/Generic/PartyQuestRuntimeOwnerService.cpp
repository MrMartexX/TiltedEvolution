#include <TiltedOnlinePCH.h>

#include <Services/PartyQuestRuntimeOwnerService.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/PartyJoinedEvent.h>
#include <Events/PartyLeftEvent.h>
#include <Events/UpdateEvent.h>
#include <PartyQuestSkyrimPapyrusRuntimeObserver.h>
#include <PartyQuestSkyrimStageMutationExecutor.h>
#include <PlayerCharacter.h>
#include <Services/QuestService.h>
#include <Structs/Skyrim/PartyQuestPlayerProfileLineage.h>
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
    m_updateConnection = aDispatcher.sink<UpdateEvent>()
        .connect<&PartyQuestRuntimeOwnerService::OnUpdate>(this);
}

PartyQuestRuntimeOwnerService::~PartyQuestRuntimeOwnerService() noexcept
{
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
    m_nextBootstrapAttempt = {};
}

void PartyQuestRuntimeOwnerService::OnDisconnected(const DisconnectedEvent&) noexcept
{
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
    m_nextBootstrapAttempt = {};
}

void PartyQuestRuntimeOwnerService::OnPartyLeft(const PartyLeftEvent&) noexcept
{
    auto& owner = PartyQuestRuntimeOwner::GetProcessOwner();
    const auto lifecycle = owner.GetSessionOwner().PrepareAndRelease(
        PartyQuestRuntimeLifecycleEvent::PartyLeave);
    LogLifecycleFailure("party-leave", lifecycle);
}

void PartyQuestRuntimeOwnerService::OnUpdate(const UpdateEvent&) noexcept
{
    auto& owner = PartyQuestRuntimeOwner::GetProcessOwner();
    if (owner.IsShutdown())
        return;

    TryBootstrap();
}

void PartyQuestRuntimeOwnerService::TryBootstrap() noexcept
{
    auto& owner = PartyQuestRuntimeOwner::GetProcessOwner();
    if (owner.IsShutdown() || !m_world.GetPartyService().IsInParty())
        return;
    if (owner.IsAcceptingOperations())
        return;

    const auto now = std::chrono::steady_clock::now();
    if (m_nextBootstrapAttempt.time_since_epoch().count() != 0 &&
        now < m_nextBootstrapAttempt)
    {
        return;
    }
    m_nextBootstrapAttempt = now + std::chrono::seconds(1);

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
    }

    // Resolve lineage on every admission attempt, including an already-owned
    // campaign. LoadGame can change the exact character while campaign and form
    // identifiers remain equal; physical owner reuse is not fresh authority.
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

    const auto bootstrap = PartyQuestRuntimeSessionBootstrap::BindProcessOwner(
        root,
        *campaign,
        lineage);

    if (bootstrap.IsBound())
    {
        spdlog::info(
            "PartyQuestRuntimeOwner production bootstrap bound: campaign={:016X}{:016X} profile={:016X}{:016X} generation={}",
            campaign->High,
            campaign->Low,
            lineage.GetProfileId().High,
            lineage.GetProfileId().Low,
            lineage.GetRuntimeGeneration());
        return;
    }

    if (bootstrap.Status != PartyQuestRuntimeSessionBootstrapStatus::UnverifiedPlayerProfile)
    {
        spdlog::debug(
            "PartyQuestRuntimeOwner bootstrap rejected fail-closed: bootstrapStatus={} ownerStatus={} generation={}",
            static_cast<uint32_t>(bootstrap.Status),
            static_cast<uint32_t>(bootstrap.Owner.Status),
            lineage.GetRuntimeGeneration());
    }
}
