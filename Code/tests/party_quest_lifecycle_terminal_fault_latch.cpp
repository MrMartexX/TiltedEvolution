#include <Structs/Skyrim/PartyQuestLifecycleTerminalFaultLatch.h>

#include <catch2/catch.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace
{
using Latch = PartyQuestLifecycleTerminalFaultLatch;
using LatchResult = PartyQuestLifecycleTerminalFaultLatchResult;
using Snapshot = PartyQuestLifecycleTerminalFaultSnapshot;
using Reason = PartyQuestOrderedLifecycleReason;
using Status = PartyQuestOrderedLifecycleEnqueueStatus;
using Evidence = PartyQuestOrderedLifecycleEvidenceMask;

constexpr std::array<Reason, 7> kReasons{
    Reason::Connected,
    Reason::PartyJoined,
    Reason::PartyLeft,
    Reason::CampaignSwitch,
    Reason::Disconnect,
    Reason::LoadGame,
    Reason::Shutdown};

constexpr std::array<Status, 3> kFatalStatuses{
    Status::QueueCapacityExceeded,
    Status::CounterExhausted,
    Status::AllocationFailed};

constexpr std::array<Status, 6> kNonFatalStatuses{
    Status::Queued,
    Status::Coalesced,
    Status::Duplicate,
    Status::TerminalQueued,
    Status::TerminalClosed,
    Status::InvalidReason};

[[nodiscard]] bool SameSnapshot(
    const Snapshot& acLeft,
    const Snapshot& acRight) noexcept
{
    return acLeft.ObservedEvidence == acRight.ObservedEvidence &&
        acLeft.FirstRejectedReason == acRight.FirstRejectedReason &&
        acLeft.Cause == acRight.Cause &&
        acLeft.Latched == acRight.Latched &&
        acLeft.ShutdownObserved == acRight.ShutdownObserved;
}

[[nodiscard]] Evidence AllReasonEvidence() noexcept
{
    Evidence evidence{};
    for (const auto reason : kReasons)
    {
        evidence = static_cast<Evidence>(
            evidence | PartyQuestOrderedLifecycleEvidenceFor(reason));
    }
    return evidence;
}
}

TEST_CASE("Terminal fault latch is empty and immutable until a fatal cause",
          "[quest.party-state][lifecycle-terminal-fault]")
{
    Latch latch;
    const auto initial = latch.Snapshot();

    REQUIRE_FALSE(initial.HasFault());
    REQUIRE_FALSE(initial.HasShutdown());
    REQUIRE(initial.ObservedEvidence == 0u);
    REQUIRE_FALSE(latch.IsLatched());
    REQUIRE_FALSE(latch.IsShutdownObserved());

    REQUIRE(latch.Observe(Reason::Connected) == LatchResult::NotLatched);
    REQUIRE(SameSnapshot(latch.Snapshot(), initial));
}

TEST_CASE("Every fatal queue cause latches exact first reason and evidence",
          "[quest.party-state][lifecycle-terminal-fault]")
{
    for (const auto status : kFatalStatuses)
    {
        for (const auto reason : kReasons)
        {
            Latch latch;
            REQUIRE(latch.LatchFatal(reason, status) == LatchResult::Latched);

            const auto snapshot = latch.Snapshot();
            REQUIRE(snapshot.HasFault());
            REQUIRE(snapshot.FirstRejectedReason == reason);
            REQUIRE(snapshot.Cause == status);
            REQUIRE(snapshot.ObservedEvidence ==
                PartyQuestOrderedLifecycleEvidenceFor(reason));
            REQUIRE(snapshot.HasShutdown() == (reason == Reason::Shutdown));
            REQUIRE(latch.IsLatched());
        }
    }
}

TEST_CASE("First terminal fault identity wins across later fatal causes",
          "[quest.party-state][lifecycle-terminal-fault]")
{
    Latch latch;
    REQUIRE(latch.LatchFatal(
                Reason::PartyLeft,
                Status::QueueCapacityExceeded) == LatchResult::Latched);

    REQUIRE(latch.LatchFatal(
                Reason::Disconnect,
                Status::CounterExhausted) == LatchResult::Observed);
    REQUIRE(latch.LatchFatal(
                Reason::CampaignSwitch,
                Status::AllocationFailed) == LatchResult::Observed);

    const auto snapshot = latch.Snapshot();
    REQUIRE(snapshot.FirstRejectedReason == Reason::PartyLeft);
    REQUIRE(snapshot.Cause == Status::QueueCapacityExceeded);
    REQUIRE(snapshot.ObservedEvidence ==
        static_cast<Evidence>(
            PartyQuestOrderedLifecycleEvidenceFor(Reason::PartyLeft) |
            PartyQuestOrderedLifecycleEvidenceFor(Reason::Disconnect) |
            PartyQuestOrderedLifecycleEvidenceFor(Reason::CampaignSwitch)));
}

TEST_CASE("Late reasons only extend terminal evidence",
          "[quest.party-state][lifecycle-terminal-fault]")
{
    Latch latch;
    REQUIRE(latch.LatchFatal(
                Reason::Connected,
                Status::AllocationFailed) == LatchResult::Latched);

    Evidence expected =
        PartyQuestOrderedLifecycleEvidenceFor(Reason::Connected);
    for (const auto reason : kReasons)
    {
        const auto before = latch.Snapshot();
        const auto result = latch.Observe(reason);
        expected = static_cast<Evidence>(
            expected | PartyQuestOrderedLifecycleEvidenceFor(reason));

        if (before.ObservedEvidence == expected &&
            (reason != Reason::Shutdown || before.HasShutdown()))
        {
            REQUIRE(result == LatchResult::DuplicateEvidence);
        }
        else
        {
            REQUIRE(result == LatchResult::Observed);
        }

        const auto snapshot = latch.Snapshot();
        REQUIRE(snapshot.FirstRejectedReason == Reason::Connected);
        REQUIRE(snapshot.Cause == Status::AllocationFailed);
        REQUIRE(snapshot.ObservedEvidence == expected);
    }

    REQUIRE(latch.Snapshot().ObservedEvidence == AllReasonEvidence());
}

TEST_CASE("Shutdown observed after a terminal fault never replaces fault identity",
          "[quest.party-state][lifecycle-terminal-fault]")
{
    Latch latch;
    REQUIRE(latch.LatchFatal(
                Reason::Disconnect,
                Status::CounterExhausted) == LatchResult::Latched);

    REQUIRE(latch.Observe(Reason::Shutdown) == LatchResult::Observed);
    const auto snapshot = latch.Snapshot();
    REQUIRE(snapshot.FirstRejectedReason == Reason::Disconnect);
    REQUIRE(snapshot.Cause == Status::CounterExhausted);
    REQUIRE(snapshot.HasShutdown());
    REQUIRE(latch.IsShutdownObserved());
    REQUIRE((snapshot.ObservedEvidence &
        PartyQuestOrderedLifecycleEvidenceFor(Reason::Shutdown)) != 0u);
}

TEST_CASE("Shutdown may be the exact first rejected terminal reason",
          "[quest.party-state][lifecycle-terminal-fault]")
{
    for (const auto status : kFatalStatuses)
    {
        Latch latch;
        REQUIRE(latch.LatchFatal(Reason::Shutdown, status) ==
            LatchResult::Latched);

        const auto snapshot = latch.Snapshot();
        REQUIRE(snapshot.FirstRejectedReason == Reason::Shutdown);
        REQUIRE(snapshot.Cause == status);
        REQUIRE(snapshot.HasShutdown());
        REQUIRE(snapshot.ObservedEvidence ==
            PartyQuestOrderedLifecycleEvidenceFor(Reason::Shutdown));
    }
}

TEST_CASE("Known nonfatal queue statuses never mutate terminal latch",
          "[quest.party-state][lifecycle-terminal-fault]")
{
    for (const auto status : kNonFatalStatuses)
    {
        Latch latch;
        const auto before = latch.Snapshot();
        REQUIRE(latch.LatchFatal(Reason::Disconnect, status) ==
            LatchResult::NonFatalStatus);
        REQUIRE(SameSnapshot(latch.Snapshot(), before));
        REQUIRE_FALSE(latch.IsLatched());
    }

    Latch latched;
    REQUIRE(latched.LatchFatal(
                Reason::Connected,
                Status::AllocationFailed) == LatchResult::Latched);
    const auto before = latched.Snapshot();
    for (const auto status : kNonFatalStatuses)
    {
        REQUIRE(latched.LatchFatal(Reason::Shutdown, status) ==
            LatchResult::NonFatalStatus);
        REQUIRE(SameSnapshot(latched.Snapshot(), before));
    }
}

TEST_CASE("Invalid status never mutates terminal latch",
          "[quest.party-state][lifecycle-terminal-fault]")
{
    const auto invalid = static_cast<Status>(0xFFu);

    Latch latch;
    const auto before = latch.Snapshot();
    REQUIRE(latch.LatchFatal(Reason::Disconnect, invalid) ==
        LatchResult::InvalidStatus);
    REQUIRE(SameSnapshot(latch.Snapshot(), before));

    REQUIRE(latch.LatchFatal(
                Reason::Connected,
                Status::QueueCapacityExceeded) == LatchResult::Latched);
    const auto latched = latch.Snapshot();
    REQUIRE(latch.LatchFatal(Reason::Shutdown, invalid) ==
        LatchResult::InvalidStatus);
    REQUIRE(SameSnapshot(latch.Snapshot(), latched));
}

TEST_CASE("Invalid reason never mutates terminal latch",
          "[quest.party-state][lifecycle-terminal-fault]")
{
    const auto invalid = static_cast<Reason>(0xFFu);

    Latch latch;
    const auto before = latch.Snapshot();
    REQUIRE(latch.LatchFatal(invalid, Status::CounterExhausted) ==
        LatchResult::InvalidReason);
    REQUIRE(latch.Observe(invalid) == LatchResult::InvalidReason);
    REQUIRE(SameSnapshot(latch.Snapshot(), before));

    REQUIRE(latch.LatchFatal(
                Reason::PartyJoined,
                Status::CounterExhausted) == LatchResult::Latched);
    const auto latched = latch.Snapshot();
    REQUIRE(latch.LatchFatal(invalid, Status::AllocationFailed) ==
        LatchResult::InvalidReason);
    REQUIRE(latch.Observe(invalid) == LatchResult::InvalidReason);
    REQUIRE(SameSnapshot(latch.Snapshot(), latched));
}

TEST_CASE("Duplicate terminal evidence is idempotent",
          "[quest.party-state][lifecycle-terminal-fault]")
{
    Latch latch;
    REQUIRE(latch.LatchFatal(
                Reason::LoadGame,
                Status::QueueCapacityExceeded) == LatchResult::Latched);
    const auto exact = latch.Snapshot();

    REQUIRE(latch.Observe(Reason::LoadGame) ==
        LatchResult::DuplicateEvidence);
    REQUIRE(SameSnapshot(latch.Snapshot(), exact));

    REQUIRE(latch.LatchFatal(
                Reason::LoadGame,
                Status::AllocationFailed) == LatchResult::DuplicateEvidence);
    REQUIRE(SameSnapshot(latch.Snapshot(), exact));
}

TEST_CASE("All late reason permutations preserve first fault and union evidence",
          "[quest.party-state][lifecycle-terminal-fault][permutations]")
{
    for (const auto status : kFatalStatuses)
    {
        auto permutation = kReasons;
        do
        {
            Latch latch;
            REQUIRE(latch.LatchFatal(permutation[0], status) ==
                LatchResult::Latched);

            for (size_t index = 1u; index < permutation.size(); ++index)
                (void)latch.Observe(permutation[index]);

            const auto snapshot = latch.Snapshot();
            REQUIRE(snapshot.FirstRejectedReason == permutation[0]);
            REQUIRE(snapshot.Cause == status);
            REQUIRE(snapshot.ObservedEvidence == AllReasonEvidence());
            REQUIRE(snapshot.HasShutdown());
        }
        while (std::next_permutation(
            permutation.begin(),
            permutation.end(),
            [](Reason aLeft, Reason aRight) noexcept
            {
                return static_cast<uint8_t>(aLeft) <
                    static_cast<uint8_t>(aRight);
            }));
    }
}

TEST_CASE("Terminal fault latch ABI and callable surface are fixed and noexcept",
          "[quest.party-state][lifecycle-terminal-fault][abi]")
{
    STATIC_REQUIRE(sizeof(PartyQuestLifecycleTerminalFaultLatchResult) == 1u);
    STATIC_REQUIRE(sizeof(PartyQuestLifecycleTerminalFaultSnapshot) == 6u);
    STATIC_REQUIRE(sizeof(PartyQuestLifecycleTerminalFaultLatch) == 6u);
    STATIC_REQUIRE(std::is_standard_layout_v<
        PartyQuestLifecycleTerminalFaultSnapshot>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<
        PartyQuestLifecycleTerminalFaultSnapshot>);

    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<Latch>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<Latch>);
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<Latch>);
    STATIC_REQUIRE_FALSE(std::is_move_assignable_v<Latch>);

    STATIC_REQUIRE(noexcept(
        std::declval<Latch&>().LatchFatal(
            Reason::Connected,
            Status::QueueCapacityExceeded)));
    STATIC_REQUIRE(noexcept(
        std::declval<Latch&>().Observe(Reason::Connected)));
    STATIC_REQUIRE(noexcept(
        std::declval<const Latch&>().Snapshot()));
    STATIC_REQUIRE(noexcept(
        std::declval<const Latch&>().IsLatched()));
    STATIC_REQUIRE(noexcept(
        std::declval<const Latch&>().IsShutdownObserved()));
}
