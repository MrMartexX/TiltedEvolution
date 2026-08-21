#include <Structs/Skyrim/PartyQuestPersistenceDurability.h>
#include <Structs/Skyrim/PartyQuestReplicaWorkspaceLease.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionStore.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>

namespace
{
const PartyQuestCampaignId kCampaign{
    0xDD11DD12DD13DD14ull,
    0xDD21DD22DD23DD24ull};
const PartyQuestPlayerProfileId kPlayer{
    0xDE11DE12DE13DE14ull,
    0xDE21DE22DE23DE24ull};
const PartyQuestPlayerProfileId kOtherPlayer{
    0xDF11DF12DF13DF14ull,
    0xDF21DF22DF23DF24ull};

struct RuntimeNamespaceSandbox
{
    std::filesystem::path Root;

    RuntimeNamespaceSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_runtime_namespace_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        ec.clear();
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~RuntimeNamespaceSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

PartyQuestCoopSavePaths BuildPaths(
    const RuntimeNamespaceSandbox& acSandbox,
    const PartyQuestPlayerProfileId& acPlayer = kPlayer)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns",
        kCampaign,
        acPlayer);
    REQUIRE(paths.has_value());
    return *paths;
}

PartyQuestPersistenceGuarantee ExpectedOwnerGuarantee() noexcept
{
#ifdef _WIN32
    return PartyQuestPersistenceGuarantee::ProcessCrashResilient;
#else
    return PartyQuestPersistenceGuarantee::PowerLossDurable;
#endif
}
} // namespace

TEST_CASE(
    "workspace capability promotes only its exact runtime namespace",
    "[quest.party-state.runtime-store][workspace-capability][durability]")
{
    RuntimeNamespaceSandbox sandbox;
    const auto paths = BuildPaths(sandbox);
    const auto otherPaths = BuildPaths(sandbox, kOtherPlayer);

    PartyQuestReplicaWorkspaceLease lease;
    REQUIRE(lease.Acquire(paths, kCampaign, kPlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    const auto capability = lease.CreatePublicationCapability(
        paths,
        kCampaign,
        kPlayer);
    REQUIRE(capability.IsVerified());
    REQUIRE(capability.Protects(paths, kCampaign, kPlayer));
    REQUIRE_FALSE(capability.Protects(otherPaths, kCampaign, kOtherPlayer));
    REQUIRE_FALSE(capability.PreparePowerLossDurableRuntimeNamespace(
        otherPaths,
        kCampaign,
        kOtherPlayer));

#ifdef _WIN32
    REQUIRE_FALSE(capability.PreparePowerLossDurableRuntimeNamespace(
        paths,
        kCampaign,
        kPlayer));
#else
    REQUIRE(capability.PreparePowerLossDurableRuntimeNamespace(
        paths,
        kCampaign,
        kPlayer));
    REQUIRE(std::filesystem::is_directory(paths.MetadataDirectory));
    REQUIRE(std::filesystem::is_directory(paths.SidecarsDirectory));
#endif
}

TEST_CASE(
    "capability-aware runtime store pins workspace authority and labels only proven durability",
    "[quest.party-state.runtime-store][workspace-capability][durability][lease-lifetime]")
{
    RuntimeNamespaceSandbox sandbox;
    const auto paths = BuildPaths(sandbox);

    PartyQuestReplicaWorkspaceLease lease;
    REQUIRE(lease.Acquire(paths, kCampaign, kPlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    const auto capability = lease.CreatePublicationCapability(
        paths,
        kCampaign,
        kPlayer);
    REQUIRE(capability.IsVerified());

    PartyQuestRuntimeApplySession session(kCampaign, kPlayer);
    const auto result = PartyQuestRuntimeSessionStore::BindAndLoad(
        session,
        paths,
        capability);
    REQUIRE(result.Status == PartyQuestRuntimeSessionStoreStatus::NewSession);
    REQUIRE(result.PersistenceStatus ==
        PartyQuestRuntimeApplyPersistenceStatus::FileNotFound);
    REQUIRE(session.GetPersistenceGuarantee() == ExpectedOwnerGuarantee());

    REQUIRE(PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee ==
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    REQUIRE_FALSE(
        PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());

    // The store captured the exact capability. Releasing the originating lease
    // object must not silently remove filesystem publication authority from the
    // bound handler.
    lease.Release();
    PartyQuestReplicaWorkspaceLease competing;
    REQUIRE(competing.Acquire(paths, kCampaign, kPlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Busy);

    // Removing the handler releases its capability copy; only then may another
    // owner acquire the same kernel-backed workspace lease.
    session.SetDurableStateHandler({});
    REQUIRE(competing.Acquire(paths, kCampaign, kPlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
}

TEST_CASE(
    "capability-aware runtime store rejects authority for another workspace",
    "[quest.party-state.runtime-store][workspace-capability][confinement]")
{
    RuntimeNamespaceSandbox sandbox;
    const auto paths = BuildPaths(sandbox);
    const auto otherPaths = BuildPaths(sandbox, kOtherPlayer);

    PartyQuestReplicaWorkspaceLease lease;
    REQUIRE(lease.Acquire(paths, kCampaign, kPlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    const auto capability = lease.CreatePublicationCapability(
        paths,
        kCampaign,
        kPlayer);
    REQUIRE(capability.IsVerified());

    PartyQuestRuntimeApplySession otherSession(kCampaign, kOtherPlayer);
    const auto rejected = PartyQuestRuntimeSessionStore::BindAndLoad(
        otherSession,
        otherPaths,
        capability);
    REQUIRE(rejected.Status == PartyQuestRuntimeSessionStoreStatus::InvalidLayout);
    REQUIRE(otherSession.GetPersistenceGuarantee() ==
        PartyQuestPersistenceGuarantee::Volatile);
    REQUIRE_FALSE(std::filesystem::exists(otherPaths.RuntimeApplySidecar));
}

TEST_CASE(
    "runtime session owner uses strong namespace only on the reviewed platform",
    "[quest.party-state.runtime-owner][workspace-capability][durability]")
{
    RuntimeNamespaceSandbox sandbox;
    const auto paths = BuildPaths(sandbox);

    PartyQuestRuntimeSessionOwner owner;
    const auto bound = owner.Bind(kCampaign, kPlayer, paths);
    REQUIRE(bound.Status == PartyQuestRuntimeSessionOwnerBindStatus::Bound);
    REQUIRE(bound.IsBound());
    REQUIRE(owner.GetRuntimeSession() != nullptr);
    REQUIRE(owner.GetRuntimeSession()->GetPersistenceGuarantee() ==
        ExpectedOwnerGuarantee());

#ifndef _WIN32
    REQUIRE(std::filesystem::is_directory(paths.MetadataDirectory));
    REQUIRE(std::filesystem::is_directory(paths.SidecarsDirectory));
#endif

    REQUIRE(PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee ==
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    REQUIRE_FALSE(
        PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());

    const auto released = owner.PrepareAndRelease(
        PartyQuestRuntimeLifecycleEvent::Shutdown);
    REQUIRE(released.CanProceed());
    REQUIRE_FALSE(owner.IsBound());
}
