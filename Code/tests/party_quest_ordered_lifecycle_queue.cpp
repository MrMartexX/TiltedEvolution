#include <Structs/Skyrim/PartyQuestOrderedLifecycleQueue.h>

#include <catch2/catch.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using Reason = PartyQuestOrderedLifecycleReason;
using Action = PartyQuestOrderedLifecycleAction;
using Status = PartyQuestOrderedLifecycleEnqueueStatus;
using Epoch = PartyQuestOrderedLifecycleEpoch;
using Claim = PartyQuestOrderedLifecycleClaim;
using Evidence = PartyQuestOrderedLifecycleEvidenceMask;

[[nodiscard]] bool SameEpoch(const Epoch& acLeft, const Epoch& acRight) noexcept
{
    return acLeft.Sequence == acRight.Sequence &&
        acLeft.Revision == acRight.Revision &&
        acLeft.Evidence == acRight.Evidence &&
        acLeft.Action == acRight.Action;
}

[[nodiscard]] bool SameClaim(const Claim& acLeft, const Claim& acRight) noexcept
{
    return acLeft.Sequence == acRight.Sequence &&
        acLeft.Revision == acRight.Revision &&
        acLeft.Evidence == acRight.Evidence &&
        acLeft.Action == acRight.Action;
}

[[nodiscard]] Evidence EvidenceOf(std::initializer_list<Reason> aReasons) noexcept
{
    Evidence result{};
    for (const auto reason : aReasons)
    {
        result = static_cast<Evidence>(
            result | PartyQuestOrderedLifecycleEvidenceFor(reason));
    }
    return result;
}

void RequireClaim(
    const std::optional<Claim>& acClaim,
    Action aAction,
    Evidence aEvidence)
{
    REQUIRE(acClaim);
    REQUIRE(acClaim->IsValid());
    REQUIRE(acClaim->Action == aAction);
    REQUIRE(acClaim->Evidence == aEvidence);
}

struct ModelQueue final
{
    std::vector<Epoch> Epochs;
    std::optional<Claim> Active;
    uint64_t NextSequence{1u};
    uint64_t NextRevision{1u};
    bool Terminal{};

    static bool IsRelease(Reason aReason) noexcept
    {
        return aReason == Reason::PartyLeft ||
            aReason == Reason::CampaignSwitch ||
            aReason == Reason::Disconnect;
    }

    static bool IsRelease(Action aAction) noexcept
    {
        return aAction == Action::ReleasePartyLeft ||
            aAction == Action::ReleaseCampaignSwitch ||
            aAction == Action::ReleaseDisconnect;
    }

    static Action ActionFor(Reason aReason) noexcept
    {
        switch (aReason)
        {
        case Reason::Connected:
            return Action::ApplyConnectedBoundary;
        case Reason::PartyJoined:
            return Action::ApplyPartyJoinedBoundary;
        case Reason::PartyLeft:
            return Action::ReleasePartyLeft;
        case Reason::CampaignSwitch:
            return Action::ReleaseCampaignSwitch;
        case Reason::Disconnect:
            return Action::ReleaseDisconnect;
        case Reason::LoadGame:
            return Action::RetireBlockedLoadAttempt;
        case Reason::Shutdown:
            return Action::ApplyShutdown;
        }

        return Action::ApplyShutdown;
    }

    static Action MergeRelease(Action aCurrent, Reason aIncoming) noexcept
    {
        if (aCurrent == Action::ReleaseDisconnect ||
            aIncoming == Reason::Disconnect)
            return Action::ReleaseDisconnect;

        if (aCurrent == Action::ReleaseCampaignSwitch ||
            aIncoming == Reason::CampaignSwitch)
            return Action::ReleaseCampaignSwitch;

        return Action::ReleasePartyLeft;
    }

    PartyQuestOrderedLifecycleEnqueueResult Enqueue(Reason aReason)
    {
        PartyQuestOrderedLifecycleEnqueueResult result;
        if (Terminal)
        {
            result.Status = Status::TerminalClosed;
            return result;
        }

        if (aReason == Reason::Shutdown)
        {
            Evidence evidence = PartyQuestOrderedLifecycleEvidenceFor(
                Reason::Shutdown);
            for (const auto& epoch : Epochs)
                evidence = static_cast<Evidence>(evidence | epoch.Evidence);

            Epoch shutdown{
                NextSequence++,
                NextRevision++,
                evidence,
                Action::ApplyShutdown};
            Epochs.assign(1u, shutdown);
            Active.reset();
            Terminal = true;
            result.Status = Status::TerminalQueued;
            result.Epoch = shutdown;
            return result;
        }

        const bool tailClaimed = Active && !Epochs.empty() &&
            Active->Sequence == Epochs.back().Sequence &&
            Active->Revision == Epochs.back().Revision;
        if (IsRelease(aReason) && !Epochs.empty() && !tailClaimed &&
            IsRelease(Epochs.back().Action))
        {
            auto& tail = Epochs.back();
            const auto evidence = static_cast<Evidence>(
                tail.Evidence |
                PartyQuestOrderedLifecycleEvidenceFor(aReason));
            const auto action = MergeRelease(tail.Action, aReason);
            if (evidence == tail.Evidence && action == tail.Action)
            {
                result.Status = Status::Duplicate;
                result.Epoch = tail;
                return result;
            }

            tail.Revision = NextRevision++;
            tail.Evidence = evidence;
            tail.Action = action;
            result.Status = Status::Coalesced;
            result.Epoch = tail;
            return result;
        }

        if (Epochs.size() >= PartyQuestOrderedLifecycleQueue::kCapacity)
        {
            result.Status = Status::QueueCapacityExceeded;
            return result;
        }

        Epoch epoch{
            NextSequence++,
            NextRevision++,
            PartyQuestOrderedLifecycleEvidenceFor(aReason),
            ActionFor(aReason)};
        Epochs.push_back(epoch);
        result.Status = Status::Queued;
        result.Epoch = epoch;
        return result;
    }

    std::optional<Claim> TryClaim()
    {
        if (Active || Epochs.empty())
            return std::nullopt;

        const auto& front = Epochs.front();
        Active = Claim{
            front.Sequence,
            front.Revision,
            front.Evidence,
            front.Action};
        return Active;
    }

    bool IsCurrent(const Claim& acClaim) const noexcept
    {
        return Active && !Epochs.empty() &&
            SameClaim(*Active, acClaim) &&
            acClaim.Sequence == Epochs.front().Sequence &&
            acClaim.Revision == Epochs.front().Revision &&
            acClaim.Evidence == Epochs.front().Evidence &&
            acClaim.Action == Epochs.front().Action;
    }

    bool Acknowledge(const Claim& acClaim)
    {
        if (!IsCurrent(acClaim))
            return false;
        Epochs.erase(Epochs.begin());
        Active.reset();
        return true;
    }

    bool Retry(const Claim& acClaim)
    {
        if (!IsCurrent(acClaim))
            return false;
        Active.reset();
        return true;
    }
};

void CompareResult(
    const PartyQuestOrderedLifecycleEnqueueResult& acActual,
    const PartyQuestOrderedLifecycleEnqueueResult& acExpected)
{
    REQUIRE(acActual.Status == acExpected.Status);
    REQUIRE(acActual.Epoch.has_value() == acExpected.Epoch.has_value());
    if (acActual.Epoch)
        REQUIRE(SameEpoch(*acActual.Epoch, *acExpected.Epoch));
}

void CompareClaim(
    const std::optional<Claim>& acActual,
    const std::optional<Claim>& acExpected)
{
    REQUIRE(acActual.has_value() == acExpected.has_value());
    if (acActual)
        REQUIRE(SameClaim(*acActual, *acExpected));
}

uint32_t NextRandom(uint32_t& aState) noexcept
{
    aState = aState * 1664525u + 1013904223u;
    return aState;
}
}

class PartyQuestOrderedLifecycleQueueTestAccess final
{
public:
    static void SetCounters(
        PartyQuestOrderedLifecycleQueue& aQueue,
        uint64_t aSequence,
        uint64_t aRevision) noexcept
    {
        aQueue.m_nextSequence = aSequence;
        aQueue.m_nextRevision = aRevision;
        aQueue.m_exhausted = false;
    }

    static size_t Size(const PartyQuestOrderedLifecycleQueue& aQueue) noexcept
    {
        return aQueue.m_epochs.size();
    }
};

TEST_CASE("Ordered lifecycle queue is empty by default",
          "[quest.party-state.ordered-lifecycle]")
{
    PartyQuestOrderedLifecycleQueue queue;
    REQUIRE_FALSE(queue.TryClaimFront());
    REQUIRE_FALSE(queue.IsTerminal());
    REQUIRE_FALSE(queue.IsExhausted());

    Claim stale{
        1u,
        1u,
        PartyQuestOrderedLifecycleEvidenceFor(Reason::Connected),
        Action::ApplyConnectedBoundary};
    REQUIRE_FALSE(queue.IsCurrent(stale));
    REQUIRE_FALSE(queue.Acknowledge(stale));
    REQUIRE_FALSE(queue.Retry(stale));
}

TEST_CASE("Ordered lifecycle queue preserves Disconnect then Connected",
          "[quest.party-state.ordered-lifecycle]")
{
    PartyQuestOrderedLifecycleQueue queue;
    REQUIRE(queue.Enqueue(Reason::Disconnect).Status == Status::Queued);
    REQUIRE(queue.Enqueue(Reason::Connected).Status == Status::Queued);

    const auto disconnect = queue.TryClaimFront();
    RequireClaim(
        disconnect,
        Action::ReleaseDisconnect,
        EvidenceOf({Reason::Disconnect}));
    REQUIRE(queue.Acknowledge(*disconnect));

    const auto connected = queue.TryClaimFront();
    RequireClaim(
        connected,
        Action::ApplyConnectedBoundary,
        EvidenceOf({Reason::Connected}));
    REQUIRE(connected->Sequence > disconnect->Sequence);
    REQUIRE(connected->Revision > disconnect->Revision);
    REQUIRE(queue.Acknowledge(*connected));
    REQUIRE_FALSE(queue.TryClaimFront());
}

TEST_CASE("Ordered lifecycle queue preserves PartyLeft then PartyJoined",
          "[quest.party-state.ordered-lifecycle]")
{
    PartyQuestOrderedLifecycleQueue queue;
    REQUIRE(queue.Enqueue(Reason::PartyLeft).Status == Status::Queued);
    REQUIRE(queue.Enqueue(Reason::PartyJoined).Status == Status::Queued);

    const auto left = queue.TryClaimFront();
    RequireClaim(
        left,
        Action::ReleasePartyLeft,
        EvidenceOf({Reason::PartyLeft}));
    REQUIRE(queue.Acknowledge(*left));

    const auto joined = queue.TryClaimFront();
    RequireClaim(
        joined,
        Action::ApplyPartyJoinedBoundary,
        EvidenceOf({Reason::PartyJoined}));
    REQUIRE(queue.Acknowledge(*joined));
    REQUIRE_FALSE(queue.TryClaimFront());
}

TEST_CASE("Acknowledged Connected is never replayed by PartyJoined retry",
          "[quest.party-state.ordered-lifecycle]")
{
    PartyQuestOrderedLifecycleQueue queue;
    REQUIRE(queue.Enqueue(Reason::Connected).Status == Status::Queued);
    REQUIRE(queue.Enqueue(Reason::PartyJoined).Status == Status::Queued);

    const auto connected = queue.TryClaimFront();
    REQUIRE(connected);
    REQUIRE(connected->Action == Action::ApplyConnectedBoundary);
    REQUIRE(queue.Acknowledge(*connected));

    const auto joined = queue.TryClaimFront();
    REQUIRE(joined);
    REQUIRE(joined->Action == Action::ApplyPartyJoinedBoundary);
    REQUIRE(queue.Retry(*joined));

    const auto retried = queue.TryClaimFront();
    REQUIRE(retried);
    REQUIRE(SameClaim(*retried, *joined));
    REQUIRE(retried->Action == Action::ApplyPartyJoinedBoundary);
    REQUIRE(queue.Acknowledge(*retried));
    REQUIRE_FALSE(queue.TryClaimFront());
}

TEST_CASE("Adjacent release epochs coalesce with explicit precedence in every order",
          "[quest.party-state.ordered-lifecycle]")
{
    std::array<Reason, 3> reasons{
        Reason::PartyLeft,
        Reason::CampaignSwitch,
        Reason::Disconnect};

    do
    {
        PartyQuestOrderedLifecycleQueue queue;
        REQUIRE(queue.Enqueue(reasons[0]).Status == Status::Queued);
        REQUIRE(queue.Enqueue(reasons[1]).Status == Status::Coalesced);
        REQUIRE(queue.Enqueue(reasons[2]).Status == Status::Coalesced);

        const auto claim = queue.TryClaimFront();
        RequireClaim(
            claim,
            Action::ReleaseDisconnect,
            EvidenceOf({
                Reason::PartyLeft,
                Reason::CampaignSwitch,
                Reason::Disconnect}));
        REQUIRE(claim->Sequence == 1u);
        REQUIRE(claim->Revision == 3u);
        REQUIRE(queue.Acknowledge(*claim));
        REQUIRE_FALSE(queue.TryClaimFront());
    }
    while (std::next_permutation(reasons.begin(), reasons.end()));
}

TEST_CASE("Connected PartyJoined and LoadGame duplicates remain separate epochs",
          "[quest.party-state.ordered-lifecycle]")
{
    for (const auto reason : {
             Reason::Connected,
             Reason::PartyJoined,
             Reason::LoadGame})
    {
        PartyQuestOrderedLifecycleQueue queue;
        const auto first = queue.Enqueue(reason);
        const auto second = queue.Enqueue(reason);
        REQUIRE(first.Status == Status::Queued);
        REQUIRE(second.Status == Status::Queued);
        REQUIRE(first.Epoch);
        REQUIRE(second.Epoch);
        REQUIRE(second.Epoch->Sequence > first.Epoch->Sequence);
        REQUIRE(second.Epoch->Revision > first.Epoch->Revision);

        const auto firstClaim = queue.TryClaimFront();
        REQUIRE(firstClaim);
        REQUIRE(firstClaim->Sequence == first.Epoch->Sequence);
        REQUIRE(queue.Acknowledge(*firstClaim));

        const auto secondClaim = queue.TryClaimFront();
        REQUIRE(secondClaim);
        REQUIRE(secondClaim->Sequence == second.Epoch->Sequence);
        REQUIRE(queue.Acknowledge(*secondClaim));
        REQUIRE_FALSE(queue.TryClaimFront());
    }
}

TEST_CASE("Rebind epochs separate release coalescing domains",
          "[quest.party-state.ordered-lifecycle]")
{
    PartyQuestOrderedLifecycleQueue queue;
    REQUIRE(queue.Enqueue(Reason::PartyLeft).Status == Status::Queued);
    REQUIRE(queue.Enqueue(Reason::CampaignSwitch).Status == Status::Coalesced);
    REQUIRE(queue.Enqueue(Reason::Connected).Status == Status::Queued);
    REQUIRE(queue.Enqueue(Reason::Disconnect).Status == Status::Queued);

    const auto first = queue.TryClaimFront();
    RequireClaim(
        first,
        Action::ReleaseCampaignSwitch,
        EvidenceOf({Reason::PartyLeft, Reason::CampaignSwitch}));
    REQUIRE(queue.Acknowledge(*first));

    const auto rebind = queue.TryClaimFront();
    RequireClaim(
        rebind,
        Action::ApplyConnectedBoundary,
        EvidenceOf({Reason::Connected}));
    REQUIRE(queue.Acknowledge(*rebind));

    const auto finalRelease = queue.TryClaimFront();
    RequireClaim(
        finalRelease,
        Action::ReleaseDisconnect,
        EvidenceOf({Reason::Disconnect}));
    REQUIRE(queue.Acknowledge(*finalRelease));
}

TEST_CASE("LoadGame is always a separate retirement epoch",
          "[quest.party-state.ordered-lifecycle]")
{
    PartyQuestOrderedLifecycleQueue queue;
    REQUIRE(queue.Enqueue(Reason::Disconnect).Status == Status::Queued);
    REQUIRE(queue.Enqueue(Reason::LoadGame).Status == Status::Queued);
    REQUIRE(queue.Enqueue(Reason::CampaignSwitch).Status == Status::Queued);

    const auto disconnect = queue.TryClaimFront();
    REQUIRE(disconnect->Action == Action::ReleaseDisconnect);
    REQUIRE(queue.Acknowledge(*disconnect));

    const auto load = queue.TryClaimFront();
    RequireClaim(
        load,
        Action::RetireBlockedLoadAttempt,
        EvidenceOf({Reason::LoadGame}));
    REQUIRE(queue.Acknowledge(*load));

    const auto campaign = queue.TryClaimFront();
    REQUIRE(campaign->Action == Action::ReleaseCampaignSwitch);
    REQUIRE(queue.Acknowledge(*campaign));
}

TEST_CASE("Duplicate release coalesces before claim but enqueues after claim",
          "[quest.party-state.ordered-lifecycle]")
{
    PartyQuestOrderedLifecycleQueue queue;
    const auto first = queue.Enqueue(Reason::PartyLeft);
    REQUIRE(first.Status == Status::Queued);

    const auto duplicate = queue.Enqueue(Reason::PartyLeft);
    REQUIRE(duplicate.Status == Status::Duplicate);
    REQUIRE(duplicate.Epoch);
    REQUIRE(first.Epoch);
    REQUIRE(SameEpoch(*duplicate.Epoch, *first.Epoch));

    const auto claim = queue.TryClaimFront();
    REQUIRE(claim);
    REQUIRE(queue.IsCurrent(*claim));

    const auto afterClaim = queue.Enqueue(Reason::PartyLeft);
    REQUIRE(afterClaim.Status == Status::Queued);
    REQUIRE(afterClaim.Epoch);
    REQUIRE(afterClaim.Epoch->Sequence > claim->Sequence);
    REQUIRE(queue.IsCurrent(*claim));

    REQUIRE(queue.Acknowledge(*claim));
    const auto later = queue.TryClaimFront();
    REQUIRE(later);
    REQUIRE(later->Action == Action::ReleasePartyLeft);
    REQUIRE(later->Sequence == afterClaim.Epoch->Sequence);
    REQUIRE(queue.Acknowledge(*later));
}

TEST_CASE("Enqueue during a claim always remains later",
          "[quest.party-state.ordered-lifecycle]")
{
    PartyQuestOrderedLifecycleQueue queue;
    REQUIRE(queue.Enqueue(Reason::Disconnect).Status == Status::Queued);
    const auto active = queue.TryClaimFront();
    REQUIRE(active);

    REQUIRE(queue.Enqueue(Reason::Connected).Status == Status::Queued);
    REQUIRE(queue.Enqueue(Reason::CampaignSwitch).Status == Status::Queued);
    REQUIRE(queue.IsCurrent(*active));
    REQUIRE_FALSE(queue.TryClaimFront());

    REQUIRE(queue.Acknowledge(*active));
    const auto connected = queue.TryClaimFront();
    REQUIRE(connected->Action == Action::ApplyConnectedBoundary);
    REQUIRE(queue.Acknowledge(*connected));

    const auto campaign = queue.TryClaimFront();
    REQUIRE(campaign->Action == Action::ReleaseCampaignSwitch);
    REQUIRE(queue.Acknowledge(*campaign));
}

TEST_CASE("Stale sequence or revision cannot acknowledge or retry",
          "[quest.party-state.ordered-lifecycle]")
{
    PartyQuestOrderedLifecycleQueue queue;
    REQUIRE(queue.Enqueue(Reason::Connected).Status == Status::Queued);
    const auto exact = queue.TryClaimFront();
    REQUIRE(exact);

    auto staleSequence = *exact;
    ++staleSequence.Sequence;
    REQUIRE_FALSE(queue.IsCurrent(staleSequence));
    REQUIRE_FALSE(queue.Acknowledge(staleSequence));
    REQUIRE_FALSE(queue.Retry(staleSequence));

    auto staleRevision = *exact;
    ++staleRevision.Revision;
    REQUIRE_FALSE(queue.IsCurrent(staleRevision));
    REQUIRE_FALSE(queue.Acknowledge(staleRevision));
    REQUIRE_FALSE(queue.Retry(staleRevision));

    REQUIRE(queue.IsCurrent(*exact));
    REQUIRE(queue.Acknowledge(*exact));
}

TEST_CASE("Retry releases only the exact claim and preserves the same front",
          "[quest.party-state.ordered-lifecycle]")
{
    PartyQuestOrderedLifecycleQueue queue;
    REQUIRE(queue.Enqueue(Reason::LoadGame).Status == Status::Queued);

    const auto first = queue.TryClaimFront();
    REQUIRE(first);
    REQUIRE(queue.Retry(*first));
    REQUIRE_FALSE(queue.IsCurrent(*first));

    const auto second = queue.TryClaimFront();
    REQUIRE(second);
    REQUIRE(SameClaim(*first, *second));
    REQUIRE(queue.Acknowledge(*second));
    REQUIRE_FALSE(queue.TryClaimFront());
}

TEST_CASE("Exact acknowledge consumes one epoch only",
          "[quest.party-state.ordered-lifecycle]")
{
    PartyQuestOrderedLifecycleQueue queue;
    REQUIRE(queue.Enqueue(Reason::Connected).Status == Status::Queued);
    REQUIRE(queue.Enqueue(Reason::PartyJoined).Status == Status::Queued);
    REQUIRE(queue.Enqueue(Reason::LoadGame).Status == Status::Queued);

    const auto first = queue.TryClaimFront();
    REQUIRE(first);
    REQUIRE(queue.Acknowledge(*first));

    const auto second = queue.TryClaimFront();
    REQUIRE(second);
    REQUIRE(second->Action == Action::ApplyPartyJoinedBoundary);
    REQUIRE(queue.Acknowledge(*second));

    const auto third = queue.TryClaimFront();
    REQUIRE(third);
    REQUIRE(third->Action == Action::RetireBlockedLoadAttempt);
    REQUIRE(queue.Acknowledge(*third));
    REQUIRE_FALSE(queue.TryClaimFront());
}

TEST_CASE("Shutdown without active claim replaces queued work and preserves evidence",
          "[quest.party-state.ordered-lifecycle]")
{
    PartyQuestOrderedLifecycleQueue queue;
    REQUIRE(queue.Enqueue(Reason::Disconnect).Status == Status::Queued);
    REQUIRE(queue.Enqueue(Reason::Connected).Status == Status::Queued);
    REQUIRE(queue.Enqueue(Reason::PartyLeft).Status == Status::Queued);

    const auto shutdown = queue.Enqueue(Reason::Shutdown);
    REQUIRE(shutdown.Status == Status::TerminalQueued);
    REQUIRE(queue.IsTerminal());

    const auto claim = queue.TryClaimFront();
    RequireClaim(
        claim,
        Action::ApplyShutdown,
        EvidenceOf({
            Reason::Disconnect,
            Reason::Connected,
            Reason::PartyLeft,
            Reason::Shutdown}));
    REQUIRE(queue.Acknowledge(*claim));
    REQUIRE_FALSE(queue.TryClaimFront());
}

TEST_CASE("Shutdown invalidates active claim and becomes the next terminal epoch",
          "[quest.party-state.ordered-lifecycle]")
{
    PartyQuestOrderedLifecycleQueue queue;
    REQUIRE(queue.Enqueue(Reason::Connected).Status == Status::Queued);
    REQUIRE(queue.Enqueue(Reason::Disconnect).Status == Status::Queued);

    const auto entered = queue.TryClaimFront();
    REQUIRE(entered);
    REQUIRE(queue.IsCurrent(*entered));

    const auto shutdown = queue.Enqueue(Reason::Shutdown);
    REQUIRE(shutdown.Status == Status::TerminalQueued);
    REQUIRE(queue.IsTerminal());
    REQUIRE_FALSE(queue.IsCurrent(*entered));
    REQUIRE_FALSE(queue.Acknowledge(*entered));
    REQUIRE_FALSE(queue.Retry(*entered));

    const auto terminal = queue.TryClaimFront();
    RequireClaim(
        terminal,
        Action::ApplyShutdown,
        EvidenceOf({
            Reason::Connected,
            Reason::Disconnect,
            Reason::Shutdown}));
    REQUIRE(queue.Acknowledge(*terminal));
}

TEST_CASE("Terminal queue rejects all later executable events",
          "[quest.party-state.ordered-lifecycle]")
{
    PartyQuestOrderedLifecycleQueue queue;
    REQUIRE(queue.Enqueue(Reason::Shutdown).Status == Status::TerminalQueued);

    for (const auto reason : {
             Reason::Connected,
             Reason::PartyJoined,
             Reason::PartyLeft,
             Reason::CampaignSwitch,
             Reason::Disconnect,
             Reason::LoadGame,
             Reason::Shutdown})
    {
        const auto rejected = queue.Enqueue(reason);
        REQUIRE(rejected.Status == Status::TerminalClosed);
        REQUIRE_FALSE(rejected.Epoch);
    }
}

TEST_CASE("Ordered lifecycle queue accepts exactly its fixed capacity",
          "[quest.party-state.ordered-lifecycle][capacity]")
{
    PartyQuestOrderedLifecycleQueue queue;
    STATIC_REQUIRE(PartyQuestOrderedLifecycleQueue::kCapacity == 32u);
    STATIC_REQUIRE(PartyQuestOrderedLifecycleQueue::kCapacity > 0u);

    for (size_t index = 0;
         index < PartyQuestOrderedLifecycleQueue::kCapacity;
         ++index)
    {
        const auto queued = queue.Enqueue(Reason::Connected);
        REQUIRE(queued.Status == Status::Queued);
        REQUIRE(queued.Epoch);
        REQUIRE(queued.Epoch->Sequence == index + 1u);
        REQUIRE(queued.Epoch->Revision == index + 1u);
    }

    REQUIRE(PartyQuestOrderedLifecycleQueueTestAccess::Size(queue) ==
        PartyQuestOrderedLifecycleQueue::kCapacity);
}

TEST_CASE("Capacity overflow does not mutate queue counters or active claim",
          "[quest.party-state.ordered-lifecycle][capacity]")
{
    PartyQuestOrderedLifecycleQueue queue;
    for (size_t index = 0;
         index < PartyQuestOrderedLifecycleQueue::kCapacity;
         ++index)
    {
        REQUIRE(queue.Enqueue(Reason::Connected).Status == Status::Queued);
    }

    const auto active = queue.TryClaimFront();
    REQUIRE(active);
    REQUIRE(queue.IsCurrent(*active));

    const auto overflow = queue.Enqueue(Reason::PartyJoined);
    REQUIRE(overflow.Status == Status::QueueCapacityExceeded);
    REQUIRE_FALSE(overflow.Epoch);
    REQUIRE(queue.IsCurrent(*active));
    REQUIRE(PartyQuestOrderedLifecycleQueueTestAccess::Size(queue) ==
        PartyQuestOrderedLifecycleQueue::kCapacity);

    REQUIRE(queue.Acknowledge(*active));

    const auto afterRoom = queue.Enqueue(Reason::PartyJoined);
    REQUIRE(afterRoom.Status == Status::Queued);
    REQUIRE(afterRoom.Epoch);
    REQUIRE(afterRoom.Epoch->Sequence ==
        PartyQuestOrderedLifecycleQueue::kCapacity + 1u);
    REQUIRE(afterRoom.Epoch->Revision ==
        PartyQuestOrderedLifecycleQueue::kCapacity + 1u);
}

TEST_CASE("Adjacent release coalescing remains admissible at capacity",
          "[quest.party-state.ordered-lifecycle][capacity]")
{
    PartyQuestOrderedLifecycleQueue queue;
    for (size_t index = 0;
         index + 1u < PartyQuestOrderedLifecycleQueue::kCapacity;
         ++index)
    {
        REQUIRE(queue.Enqueue(Reason::Connected).Status == Status::Queued);
    }

    REQUIRE(queue.Enqueue(Reason::PartyLeft).Status == Status::Queued);
    REQUIRE(PartyQuestOrderedLifecycleQueueTestAccess::Size(queue) ==
        PartyQuestOrderedLifecycleQueue::kCapacity);

    const auto coalesced = queue.Enqueue(Reason::Disconnect);
    REQUIRE(coalesced.Status == Status::Coalesced);
    REQUIRE(coalesced.Epoch);
    REQUIRE(coalesced.Epoch->Action == Action::ReleaseDisconnect);
    REQUIRE(coalesced.Epoch->Evidence ==
        EvidenceOf({Reason::PartyLeft, Reason::Disconnect}));
    REQUIRE(coalesced.Epoch->Sequence ==
        PartyQuestOrderedLifecycleQueue::kCapacity);
    REQUIRE(coalesced.Epoch->Revision ==
        PartyQuestOrderedLifecycleQueue::kCapacity + 1u);
    REQUIRE(PartyQuestOrderedLifecycleQueueTestAccess::Size(queue) ==
        PartyQuestOrderedLifecycleQueue::kCapacity);
}

TEST_CASE("Claimed front stays current when enqueue overflows at capacity",
          "[quest.party-state.ordered-lifecycle][capacity]")
{
    PartyQuestOrderedLifecycleQueue queue;
    for (size_t index = 0;
         index < PartyQuestOrderedLifecycleQueue::kCapacity;
         ++index)
    {
        REQUIRE(queue.Enqueue(Reason::Connected).Status == Status::Queued);
    }

    const auto active = queue.TryClaimFront();
    REQUIRE(active);
    const auto overflow = queue.Enqueue(Reason::LoadGame);
    REQUIRE(overflow.Status == Status::QueueCapacityExceeded);
    REQUIRE(queue.IsCurrent(*active));

    REQUIRE(queue.Retry(*active));
    const auto retried = queue.TryClaimFront();
    REQUIRE(retried);
    REQUIRE(SameClaim(*retried, *active));
    REQUIRE(queue.Acknowledge(*retried));
}

TEST_CASE("Shutdown atomically replaces a full queue and preserves evidence",
          "[quest.party-state.ordered-lifecycle][capacity]")
{
    PartyQuestOrderedLifecycleQueue queue;
    Evidence expected = PartyQuestOrderedLifecycleEvidenceFor(Reason::Shutdown);

    for (size_t index = 0;
         index < PartyQuestOrderedLifecycleQueue::kCapacity;
         ++index)
    {
        const auto reason = (index % 2u == 0u)
            ? Reason::Connected
            : Reason::LoadGame;
        REQUIRE(queue.Enqueue(reason).Status == Status::Queued);
        expected = static_cast<Evidence>(
            expected | PartyQuestOrderedLifecycleEvidenceFor(reason));
    }

    const auto shutdown = queue.Enqueue(Reason::Shutdown);
    REQUIRE(shutdown.Status == Status::TerminalQueued);
    REQUIRE(shutdown.Epoch);
    REQUIRE(shutdown.Epoch->Evidence == expected);
    REQUIRE(shutdown.Epoch->Action == Action::ApplyShutdown);
    REQUIRE(queue.IsTerminal());
    REQUIRE(PartyQuestOrderedLifecycleQueueTestAccess::Size(queue) == 1u);

    const auto claim = queue.TryClaimFront();
    REQUIRE(claim);
    REQUIRE(claim->Evidence == expected);
    REQUIRE(queue.Acknowledge(*claim));
}

TEST_CASE("Retry and acknowledge remain exact after a capacity overflow",
          "[quest.party-state.ordered-lifecycle][capacity]")
{
    PartyQuestOrderedLifecycleQueue queue;
    for (size_t index = 0;
         index < PartyQuestOrderedLifecycleQueue::kCapacity;
         ++index)
    {
        REQUIRE(queue.Enqueue(Reason::Connected).Status == Status::Queued);
    }

    const auto first = queue.TryClaimFront();
    REQUIRE(first);
    REQUIRE(queue.Enqueue(Reason::PartyJoined).Status ==
        Status::QueueCapacityExceeded);

    REQUIRE(queue.Retry(*first));
    const auto retried = queue.TryClaimFront();
    REQUIRE(retried);
    REQUIRE(SameClaim(*retried, *first));

    auto stale = *retried;
    ++stale.Revision;
    REQUIRE_FALSE(queue.Acknowledge(stale));
    REQUIRE(queue.IsCurrent(*retried));
    REQUIRE(queue.Acknowledge(*retried));
}

TEST_CASE("Sequence counter exhaustion fails closed without wrapping",
          "[quest.party-state.ordered-lifecycle]")
{
    PartyQuestOrderedLifecycleQueue queue;
    PartyQuestOrderedLifecycleQueueTestAccess::SetCounters(
        queue,
        std::numeric_limits<uint64_t>::max(),
        100u);

    const auto last = queue.Enqueue(Reason::Connected);
    REQUIRE(last.Status == Status::Queued);
    REQUIRE(last.Epoch);
    REQUIRE(last.Epoch->Sequence == std::numeric_limits<uint64_t>::max());
    REQUIRE(last.Epoch->Revision == 100u);
    REQUIRE(queue.IsExhausted());

    const auto rejected = queue.Enqueue(Reason::PartyJoined);
    REQUIRE(rejected.Status == Status::CounterExhausted);
    REQUIRE_FALSE(rejected.Epoch);

    const auto claim = queue.TryClaimFront();
    REQUIRE(claim);
    REQUIRE(claim->Sequence == std::numeric_limits<uint64_t>::max());
    REQUIRE(queue.Acknowledge(*claim));
}

TEST_CASE("Revision counter exhaustion fails closed after final coalescing revision",
          "[quest.party-state.ordered-lifecycle]")
{
    PartyQuestOrderedLifecycleQueue queue;
    REQUIRE(queue.Enqueue(Reason::PartyLeft).Status == Status::Queued);

    PartyQuestOrderedLifecycleQueueTestAccess::SetCounters(
        queue,
        10u,
        std::numeric_limits<uint64_t>::max());

    const auto lastRevision = queue.Enqueue(Reason::CampaignSwitch);
    REQUIRE(lastRevision.Status == Status::Coalesced);
    REQUIRE(lastRevision.Epoch);
    REQUIRE(lastRevision.Epoch->Sequence == 1u);
    REQUIRE(lastRevision.Epoch->Revision ==
        std::numeric_limits<uint64_t>::max());
    REQUIRE(queue.IsExhausted());

    const auto rejected = queue.Enqueue(Reason::Disconnect);
    REQUIRE(rejected.Status == Status::CounterExhausted);
    REQUIRE_FALSE(rejected.Epoch);
}

TEST_CASE("Invalid reason is rejected without consuming counters",
          "[quest.party-state.ordered-lifecycle]")
{
    PartyQuestOrderedLifecycleQueue queue;
    const auto invalid = queue.Enqueue(
        static_cast<Reason>(0xFFu));
    REQUIRE(invalid.Status == Status::InvalidReason);
    REQUIRE_FALSE(invalid.Epoch);

    const auto valid = queue.Enqueue(Reason::Connected);
    REQUIRE(valid.Status == Status::Queued);
    REQUIRE(valid.Epoch);
    REQUIRE(valid.Epoch->Sequence == 1u);
    REQUIRE(valid.Epoch->Revision == 1u);
}

TEST_CASE("Ordered lifecycle queue is move-only and preserves active state on move",
          "[quest.party-state.ordered-lifecycle]")
{
    STATIC_REQUIRE_FALSE(
        std::is_copy_constructible_v<PartyQuestOrderedLifecycleQueue>);
    STATIC_REQUIRE_FALSE(
        std::is_copy_assignable_v<PartyQuestOrderedLifecycleQueue>);
    STATIC_REQUIRE(
        std::is_nothrow_move_constructible_v<PartyQuestOrderedLifecycleQueue>);
    STATIC_REQUIRE(
        std::is_nothrow_move_assignable_v<PartyQuestOrderedLifecycleQueue>);
    STATIC_REQUIRE(sizeof(PartyQuestOrderedLifecycleReason) == 1u);
    STATIC_REQUIRE(sizeof(PartyQuestOrderedLifecycleAction) == 1u);
    STATIC_REQUIRE(sizeof(PartyQuestOrderedLifecycleEvidenceMask) == 2u);
    STATIC_REQUIRE(noexcept(
        std::declval<PartyQuestOrderedLifecycleQueue&>().Enqueue(
            Reason::Connected)));
    STATIC_REQUIRE(noexcept(
        std::declval<PartyQuestOrderedLifecycleQueue&>().TryClaimFront()));
    STATIC_REQUIRE(noexcept(
        std::declval<const PartyQuestOrderedLifecycleQueue&>().IsCurrent(
            std::declval<const Claim&>())));
    STATIC_REQUIRE(noexcept(
        std::declval<PartyQuestOrderedLifecycleQueue&>().Acknowledge(
            std::declval<const Claim&>())));
    STATIC_REQUIRE(noexcept(
        std::declval<PartyQuestOrderedLifecycleQueue&>().Retry(
            std::declval<const Claim&>())));

    PartyQuestOrderedLifecycleQueue source;
    REQUIRE(source.Enqueue(Reason::Connected).Status == Status::Queued);
    const auto claim = source.TryClaimFront();
    REQUIRE(claim);

    PartyQuestOrderedLifecycleQueue moved(std::move(source));
    REQUIRE(moved.IsCurrent(*claim));
    REQUIRE(moved.Acknowledge(*claim));
    REQUIRE_FALSE(moved.TryClaimFront());
}

TEST_CASE("Deterministic randomized bounded model preserves capacity ordering claim and retry invariants",
          "[quest.party-state.ordered-lifecycle][model]")
{
    PartyQuestOrderedLifecycleQueue queue;
    ModelQueue model;
    uint32_t random = 0xC001D00Du;

    const std::array<Reason, 6> executable{
        Reason::Connected,
        Reason::PartyJoined,
        Reason::PartyLeft,
        Reason::CampaignSwitch,
        Reason::Disconnect,
        Reason::LoadGame};

    for (size_t step = 0; step < 2000u; ++step)
    {
        const uint32_t value = NextRandom(random);
        switch (value % 5u)
        {
        case 0:
        case 1:
        {
            const auto reason = executable[
                NextRandom(random) % executable.size()];
            CompareResult(queue.Enqueue(reason), model.Enqueue(reason));
            break;
        }
        case 2:
            CompareClaim(queue.TryClaimFront(), model.TryClaim());
            break;
        case 3:
        {
            if (!model.Active)
            {
                CompareClaim(queue.TryClaimFront(), model.TryClaim());
                break;
            }

            Claim candidate = *model.Active;
            if ((NextRandom(random) & 3u) == 0u)
                ++candidate.Revision;

            REQUIRE(queue.Acknowledge(candidate) ==
                model.Acknowledge(candidate));
            break;
        }
        case 4:
        {
            if (!model.Active)
            {
                CompareClaim(queue.TryClaimFront(), model.TryClaim());
                break;
            }

            Claim candidate = *model.Active;
            if ((NextRandom(random) & 3u) == 0u)
                ++candidate.Sequence;

            REQUIRE(queue.Retry(candidate) == model.Retry(candidate));
            break;
        }
        }

        if (model.Active)
            REQUIRE(queue.IsCurrent(*model.Active));
        REQUIRE(queue.IsTerminal() == model.Terminal);
        REQUIRE(PartyQuestOrderedLifecycleQueueTestAccess::Size(queue) ==
            model.Epochs.size());
        REQUIRE(model.Epochs.size() <=
            PartyQuestOrderedLifecycleQueue::kCapacity);
    }

    CompareResult(queue.Enqueue(Reason::Shutdown), model.Enqueue(Reason::Shutdown));
    REQUIRE(queue.IsTerminal());
    REQUIRE(model.Terminal);

    for (const auto reason : executable)
        CompareResult(queue.Enqueue(reason), model.Enqueue(reason));

    CompareClaim(queue.TryClaimFront(), model.TryClaim());
    REQUIRE(model.Active);
    REQUIRE(model.Active->Action == Action::ApplyShutdown);
    REQUIRE(queue.IsCurrent(*model.Active));
    REQUIRE(queue.Acknowledge(*model.Active) ==
        model.Acknowledge(*model.Active));
    REQUIRE_FALSE(queue.TryClaimFront());
}
