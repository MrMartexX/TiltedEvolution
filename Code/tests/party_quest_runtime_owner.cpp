#include <Structs/Skyrim/PartyQuestRuntimeOwner.h>

#include <catch2/catch.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <thread>

namespace
{
const PartyQuestCampaignId kRuntimeOwnerCampaign{
    0xA101A102A103A104ull,
    0xA105A106A107A108ull};
const PartyQuestPlayerProfileId kRuntimeOwnerProfile{
    0xB101B102B103B104ull,
    0xB105B106B107B108ull};

struct RuntimeOwnerSandbox
{
    std::filesystem::path Root;

    RuntimeOwnerSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now()
            .time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_runtime_aggregate_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        ec.clear();
        std::filesystem::create_directories(Root / "CoopCampaigns", ec);
        REQUIRE_FALSE(ec);
    }

    ~RuntimeOwnerSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

void MakeRuntimeOwnerReady(
    PartyQuestRuntimeOwner& aOwner,
    const std::filesystem::path& acRoot)
{
    REQUIRE(aOwner.ApplyClientBoundary(
                PartyQuestRuntimeOwner::ClientBoundary::Connected) ==
        PartyQuestRuntimeOwner::BoundaryStatus::Applied);
    REQUIRE(aOwner.ApplyClientBoundary(
                PartyQuestRuntimeOwner::ClientBoundary::PartyJoined) ==
        PartyQuestRuntimeOwner::BoundaryStatus::Applied);

    const auto paths = PartyQuestCoopSaveLayout::Build(
        acRoot / "CoopCampaigns",
        kRuntimeOwnerCampaign,
        kRuntimeOwnerProfile);
    REQUIRE(paths.has_value());

    const auto bound = aOwner.GetSessionOwner().Bind(
        kRuntimeOwnerCampaign,
        kRuntimeOwnerProfile,
        *paths);
    REQUIRE(bound.IsBound());

    const uint64_t generation =
        PartyQuestRuntimeGenerationFence::GetProcessFence().GetGeneration();
    REQUIRE(generation != 0);
    aOwner.MarkRuntimeSessionBound(generation);
    REQUIRE(aOwner.IsAcceptingOperations());
}
} // namespace

TEST_CASE(
    "Runtime aggregate callback is inert after owner destruction",
    "[quest.party-state.runtime-owner][lifetime]")
{
    RuntimeOwnerSandbox sandbox;
    std::atomic_uint32_t executed{};
    std::function<void()> callback;

    {
        auto owner = std::make_unique<PartyQuestRuntimeOwner>();
        MakeRuntimeOwnerReady(*owner, sandbox.Root);
        const auto queued = owner->Enqueue(
            [] { return true; },
            [&] { ++executed; });
        REQUIRE(queued.Status == PartyQuestRuntimeOwner::EnqueueStatus::Queued);
        callback = owner->MakeExecuteNextCallback();
        REQUIRE(callback);
    }

    REQUIRE_NOTHROW(callback());
    REQUIRE(executed.load() == 0);
}

TEST_CASE(
    "Runtime aggregate enqueue racing disconnect cannot survive the boundary",
    "[quest.party-state.runtime-owner][lifecycle][race]")
{
    RuntimeOwnerSandbox sandbox;
    PartyQuestRuntimeOwner owner;
    MakeRuntimeOwnerReady(owner, sandbox.Root);

    std::atomic_bool start{};
    std::atomic_uint32_t executed{};
    PartyQuestRuntimeOwner::EnqueueResult enqueueResult;

    std::thread enqueueThread([&]
    {
        while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();
        enqueueResult = owner.Enqueue(
            [] { return true; },
            [&] { ++executed; });
    });

    start.store(true, std::memory_order_release);
    const auto boundary = owner.ApplyClientBoundary(
        PartyQuestRuntimeOwner::ClientBoundary::Disconnected);
    enqueueThread.join();

    REQUIRE(boundary == PartyQuestRuntimeOwner::BoundaryStatus::Applied);
    REQUIRE((enqueueResult.Status == PartyQuestRuntimeOwner::EnqueueStatus::Queued ||
             enqueueResult.Status == PartyQuestRuntimeOwner::EnqueueStatus::AdmissionClosed ||
             enqueueResult.Status == PartyQuestRuntimeOwner::EnqueueStatus::SynchronizationFailed));
    REQUIRE(owner.GetPendingOperationCount() == 0);
    REQUIRE_FALSE(owner.IsAcceptingOperations());
    REQUIRE(owner.ExecuteNext() != PartyQuestRuntimeOwner::ExecuteStatus::Executed);
    REQUIRE(executed.load() == 0);
}

TEST_CASE(
    "LoadGame cannot cross runtime validation to execution lease",
    "[quest.party-state.runtime-owner][lifecycle][race]")
{
    RuntimeOwnerSandbox sandbox;
    PartyQuestRuntimeOwner owner;
    MakeRuntimeOwnerReady(owner, sandbox.Root);

    std::promise<void> validationEnteredPromise;
    auto validationEntered = validationEnteredPromise.get_future();
    std::promise<void> allowValidationReturnPromise;
    auto allowValidationReturn = allowValidationReturnPromise.get_future().share();
    std::atomic_bool executorEntered{};
    std::atomic_bool lifecycleReturned{};

    REQUIRE(owner.Enqueue(
                [&]
                {
                    validationEnteredPromise.set_value();
                    allowValidationReturn.wait();
                    return true;
                },
                [&]
                {
                    executorEntered.store(true, std::memory_order_release);
                }).Status == PartyQuestRuntimeOwner::EnqueueStatus::Queued);

    PartyQuestRuntimeOwner::ExecuteStatus executeStatus{};
    std::thread executionThread([&]
    {
        executeStatus = owner.ExecuteNext();
    });

    REQUIRE(validationEntered.wait_for(std::chrono::seconds(2)) ==
        std::future_status::ready);

    PartyQuestRuntimeLifecycleFenceResult lifecycle;
    std::thread lifecycleThread([&]
    {
        lifecycle = owner.GetSessionOwner().PrepareAndRelease(
            PartyQuestRuntimeLifecycleEvent::LoadGame);
        lifecycleReturned.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE_FALSE(lifecycleReturned.load(std::memory_order_acquire));
    REQUIRE_FALSE(executorEntered.load(std::memory_order_acquire));

    allowValidationReturnPromise.set_value();
    executionThread.join();
    lifecycleThread.join();

    REQUIRE(executeStatus == PartyQuestRuntimeOwner::ExecuteStatus::Executed);
    REQUIRE(executorEntered.load(std::memory_order_acquire));
    REQUIRE(lifecycleReturned.load(std::memory_order_acquire));
    REQUIRE(lifecycle.CanProceed());
}

TEST_CASE(
    "Runtime aggregate can be recreated without retaining old callbacks or lease",
    "[quest.party-state.runtime-owner][lifetime]")
{
    RuntimeOwnerSandbox sandbox;
    std::function<void()> staleCallback;

    {
        PartyQuestRuntimeOwner first;
        MakeRuntimeOwnerReady(first, sandbox.Root);
        staleCallback = first.MakeExecuteNextCallback();
        REQUIRE(staleCallback);
    }

    PartyQuestRuntimeOwner second;
    MakeRuntimeOwnerReady(second, sandbox.Root);
    REQUIRE(second.IsAcceptingOperations());
    REQUIRE_NOTHROW(staleCallback());
    REQUIRE(second.GetPendingOperationCount() == 0);
}

TEST_CASE(
    "Runtime aggregate shutdown clears pending work and permanently closes admission",
    "[quest.party-state.runtime-owner][shutdown]")
{
    RuntimeOwnerSandbox sandbox;
    PartyQuestRuntimeOwner owner;
    MakeRuntimeOwnerReady(owner, sandbox.Root);

    std::atomic_uint32_t skyrimAccess{};
    REQUIRE(owner.Enqueue(
                [] { return true; },
                [&] { ++skyrimAccess; }).Status ==
        PartyQuestRuntimeOwner::EnqueueStatus::Queued);
    REQUIRE(owner.GetPendingOperationCount() == 1);

    REQUIRE(owner.ApplyClientBoundary(
                PartyQuestRuntimeOwner::ClientBoundary::Shutdown) ==
        PartyQuestRuntimeOwner::BoundaryStatus::Applied);
    REQUIRE(owner.IsShutdown());
    REQUIRE(owner.GetPendingOperationCount() == 0);
    REQUIRE_FALSE(owner.IsAcceptingOperations());
    REQUIRE(owner.Enqueue(
                [] { return true; },
                [&] { ++skyrimAccess; }).Status ==
        PartyQuestRuntimeOwner::EnqueueStatus::AdmissionClosed);
    REQUIRE(skyrimAccess.load() == 0);
}

TEST_CASE(
    "Runtime aggregate bootstrap exception fails closed",
    "[quest.party-state.runtime-owner][bootstrap]")
{
    PartyQuestRuntimeOwner owner;
    const uint64_t generation =
        PartyQuestRuntimeGenerationFence::GetProcessFence().GetGeneration();
    REQUIRE(generation != 0);

    const auto result = owner.RunBootstrap(
        generation,
        []() -> bool
        {
            throw 7;
        });

    REQUIRE(result.Status == PartyQuestRuntimeOwner::BootstrapStatus::Exception);
    REQUIRE_FALSE(owner.IsAcceptingOperations());
    REQUIRE(owner.GetPendingOperationCount() == 0);
}

TEST_CASE(
    "Runtime aggregate never touches Skyrim after lifecycle gate closes",
    "[quest.party-state.runtime-owner][lifecycle][safety]")
{
    RuntimeOwnerSandbox sandbox;
    PartyQuestRuntimeOwner owner;
    MakeRuntimeOwnerReady(owner, sandbox.Root);

    std::atomic_uint32_t skyrimAccess{};
    owner.ConfigureRuntimeAdapters(
        [&]
        {
            ++skyrimAccess;
            return true;
        },
        [](uint64_t)
        {
            return PartyQuestPapyrusRuntimeObservation{};
        },
        [&](const PartyQuestRuntimeApplyRequest&)
        {
            ++skyrimAccess;
            return true;
        });

    REQUIRE(owner.Enqueue(
                [] { return true; },
                [&] { ++skyrimAccess; }).Status ==
        PartyQuestRuntimeOwner::EnqueueStatus::Queued);

    REQUIRE(owner.ApplyClientBoundary(
                PartyQuestRuntimeOwner::ClientBoundary::RuntimeIdentityChanged) ==
        PartyQuestRuntimeOwner::BoundaryStatus::Applied);

    REQUIRE(owner.ExecuteNext() == PartyQuestRuntimeOwner::ExecuteStatus::Empty);
    REQUIRE_FALSE(owner.ObserveSkyrimRuntime());
    REQUIRE_FALSE(owner.ObservePapyrusRuntime(1).has_value());
    REQUIRE(skyrimAccess.load() == 0);
}
