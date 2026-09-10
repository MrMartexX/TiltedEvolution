#pragma once

#include <Structs/Skyrim/PartyQuestProtocol.h>

#include <cstdint>

enum class PartyQuestShadowPeerState : uint8_t
{
    Idle,
    WaitingForBaseline,
    WaitingForMissedUpdate,
    Passed,
    Failed
};

enum class PartyQuestShadowPeerFailure : uint8_t
{
    None,
    InvalidCampaign,
    ConnectFailed,
    InitialSyncFailed,
    BaselineApplyFailed,
    DisconnectFailed,
    ReconnectFailed,
    MissedUpdateRepairFailed,
    DigestMutationFailed,
    DigestRepairFailed
};

struct PartyQuestShadowPeerMetrics
{
    uint64_t StartWorldRevision{};
    uint64_t BaselineWorldRevision{};
    uint64_t MissedWorldRevision{};
    uint64_t FinalWorldRevision{};
    PartyQuestRepairSummary InitialSyncSummary;
    PartyQuestRepairSummary MissedUpdateRepairSummary;
    PartyQuestRepairSummary DigestRepairSummary;
};

/**
 * Server-local second diagnostic replica used when only one real Skyrim client
 * is available.
 *
 * The harness deliberately stays outside Skyrim runtime mutation. It connects
 * to PartyQuestProtocolCoordinator as a synthetic client, converges on the
 * current campaign, receives one canonical update, deliberately disconnects
 * for the next update, reconnects and verifies deterministic repair, then
 * creates a same-revision digest divergence and verifies a second repair.
 *
 * This exercises the same game-independent report/repair/ACK path as a second
 * client without requiring another Skyrim process, account, or computer.
 */
class PartyQuestShadowPeerHarness final
{
public:
    static constexpr uint32_t kClientId = 0xFFFFFFFEu;

    PartyQuestShadowPeerHarness()
        : m_client(kClientId)
    {
    }

    [[nodiscard]] bool Start(
        PartyQuestProtocolCoordinator& aCoordinator,
        const PartyQuestCampaignId& acCampaignId);

    void HandleCanonicalUpdate(
        PartyQuestProtocolCoordinator& aCoordinator,
        const NotifyPartyQuestCanonicalUpdate& acUpdate);

    [[nodiscard]] PartyQuestShadowPeerState GetState() const noexcept { return m_state; }
    [[nodiscard]] PartyQuestShadowPeerFailure GetFailure() const noexcept { return m_failure; }
    [[nodiscard]] const PartyQuestShadowPeerMetrics& GetMetrics() const noexcept { return m_metrics; }
    [[nodiscard]] const PartyQuestClientSession& GetClient() const noexcept { return m_client; }

private:
    enum class ExpectedRepair : uint8_t
    {
        Any,
        MissedUpdate,
        DigestMismatch
    };

    [[nodiscard]] bool Synchronize(
        PartyQuestProtocolCoordinator& aCoordinator,
        bool aReconnect,
        ExpectedRepair aExpectedRepair,
        PartyQuestRepairSummary& aSummary);

    void Fail(PartyQuestShadowPeerFailure aFailure) noexcept;

    PartyQuestShadowPeerState m_state{PartyQuestShadowPeerState::Idle};
    PartyQuestShadowPeerFailure m_failure{PartyQuestShadowPeerFailure::None};
    PartyQuestCampaignId m_campaignId;
    PartyQuestClientSession m_client;
    PartyQuestShadowPeerMetrics m_metrics;
    uint64_t m_nextReportId{1};
};
