#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeOwner.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionBootstrap.h>

#include <party_quest_runtime_session_owner_test_access.h>

#include <catch2/catch.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <new>
#include <thread>

class PartyQuestPlayerProfileLineageTestAccess final
{
public:
    static PartyQuestPlayerProfileLineageAuthorization Issue(
        PartyQuestPlayerProfileId aProfileId,
        uint64_t aRuntimeGeneration,
        bool aExactCharacterLineage = true,
        bool aPersistedWithCharacterLineage = true,
        bool aFilenameIndependent = true) noexcept
    {
        return PartyQuestPlayerProfileLineageAuthorization(
            aProfileId,
            aRuntimeGeneration,
            aExactCharacterLineage,
            aPersistedWithCharacterLineage,
            aFilenameIndependent);
    }

    static PartyQuestPlayerProfileLineageAuthorization ResolveBridgeSnapshots(
        const PartyQuestLineageProviderDescriptor& acProvider,
        const PartyQuestLineageRuntimeVersion& acExpectedRuntime,
        const PartyQuestLineageBridgeSnapshot& acFirst,
        const PartyQuestLineageBridgeSnapshot& acSecond,
        uint64_t aRuntimeGeneration) noexcept
    {
        return PartyQuestSkyrimPlayerProfileLineageResolver::
            ResolveStableSnapshots(
                acProvider,
                acExpectedRuntime,
                acFirst,
                acSecond,
                aRuntimeGeneration);
    }
};

class PartyQuestRuntimeSessionBootstrapConcurrencyTestAccess final
{
public:
    [[nodiscard]] static PartyQuestRuntimeSessionBootstrapResult BindWithLeaseHook(
        const std::filesystem::path& acCoopReplicaRoot,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileLineageAuthorization& acPlayerProfile,
        const std::function<void()>& acAfterGenerationLeaseAcquired) noexcept
    {
        return PartyQuestRuntimeSessionBootstrap::BindProcessOwnerInternal(
            acCoopReplicaRoot,
            acCampaignId,
            acPlayerProfile,
            false,
            acAfterGenerationLeaseAcquired);
    }
};

namespace
{
const PartyQuestCampaignId kConcurrentBootstrapCampaign{
    0xCA01CA02CA03CA04ull,
    0xCA05CA06CA07CA08ull};
const PartyQuestPlayerProfileId kConcurrentBootstrapProfile{
    0xCB01CB02CB03CB04ull,
    0xCB05CB06CB07CB08ull};

struct ConcurrentBootstrapSandbox final
{
    std::filesystem::path Root;
    std::filesystem::path CoopRoot;

    ConcurrentBootstrapSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now()
            .time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_runtime_bootstrap_race_" + std::to_string(nonce));
        CoopRoot = Root / "CoopCampaigns";
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        ec.clear();
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
        REQUIRE(CoopRoot.is_absolute());
    }

    ~ConcurrentBootstrapSandbox()
    {
        PartyQuestRuntimeSessionOwnerTestAccess::ForceClearProcessOwner();
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};
} // namespace

TEST_CASE(
    "Lifecycle invalidation cannot cross runtime owner bind publication",
    "[quest.party-state.runtime-bootstrap][generation][lifecycle][race][atomic]")
{
    ConcurrentBootstrapSandbox sandbox;
    PartyQuestRuntimeSessionOwnerTestAccess::ForceClearProcessOwner();

    auto& owner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const uint64_t generation = fence.GetGeneration();
    REQUIRE(generation != 0);

    const auto authorization = PartyQuestPlayerProfileLineageTestAccess::Issue(
        kConcurrentBootstrapProfile,
        generation);
    REQUIRE(authorization.IsVerified());

    std::promise<void> leaseEnteredPromise;
    auto leaseEntered = leaseEnteredPromise.get_future();
    std::promise<void> releaseLeasePromise;
    auto releaseLease = releaseLeasePromise.get_future().share();

    PartyQuestRuntimeSessionBootstrapResult bindResult;
    std::thread bindThread([&]
    {
        bindResult =
            PartyQuestRuntimeSessionBootstrapConcurrencyTestAccess::BindWithLeaseHook(
                sandbox.CoopRoot,
                kConcurrentBootstrapCampaign,
                authorization,
                [&]
                {
                    leaseEnteredPromise.set_value();
                    releaseLease.wait();
                });
    });

    REQUIRE(leaseEntered.wait_for(std::chrono::seconds(2)) ==
        std::future_status::ready);

    std::atomic_bool lifecycleReturned{};
    PartyQuestRuntimeLifecycleFenceResult lifecycleResult;
    std::thread lifecycleThread([&]
    {
        lifecycleResult = owner.PrepareAndRelease(
            PartyQuestRuntimeLifecycleEvent::LoadGame);
        lifecycleReturned.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE_FALSE(lifecycleReturned.load(std::memory_order_acquire));
    REQUIRE_FALSE(owner.IsBound());

    releaseLeasePromise.set_value();
    bindThread.join();
    lifecycleThread.join();

    REQUIRE(bindResult.Status == PartyQuestRuntimeSessionBootstrapStatus::Bound);
    REQUIRE(bindResult.IsBound());
    REQUIRE(lifecycleResult.CanProceed());
    REQUIRE(lifecycleReturned.load(std::memory_order_acquire));
    REQUIRE(fence.GetGeneration() != generation);
    REQUIRE_FALSE(owner.IsBound());
}

TEST_CASE(
    "Runtime aggregate allocation failure during bootstrap remains fail closed",
    "[quest.party-state.runtime-owner][bootstrap][allocation-failure]")
{
    PartyQuestRuntimeOwner owner;
    const uint64_t generation =
        PartyQuestRuntimeGenerationFence::GetProcessFence().GetGeneration();
    REQUIRE(generation != 0);

    const auto result = owner.RunBootstrap(
        generation,
        []() -> bool
        {
            throw std::bad_alloc{};
        });

    REQUIRE(result.Status == PartyQuestRuntimeOwner::BootstrapStatus::Exception);
    REQUIRE_FALSE(owner.IsAcceptingOperations());
    REQUIRE(owner.GetPendingOperationCount() == 0);
}
