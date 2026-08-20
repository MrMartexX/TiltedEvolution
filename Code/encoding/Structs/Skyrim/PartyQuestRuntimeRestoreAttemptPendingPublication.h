#pragma once

#include <Structs/Skyrim/PartyQuestDurableResourcePolicy.h>
#include <Structs/Skyrim/PartyQuestRuntimeRestoreAttempt.h>

#include <cstdint>
#include <filesystem>
#include <system_error>

enum class PartyQuestRuntimeRestoreAttemptPendingPublicationStatus : uint8_t
{
    Missing,
    Present,
    ProbeFailed
};

/**
 * Non-mutating probe for the staged attempt-state publication used by the
 * power-loss durable writer.
 *
 * A primary-missing RuntimeTransaction_<TransactionId>.bin.tmp cannot be
 * treated as absence: EnsureInitializedAuthorized() explicitly recognizes that
 * archive as recoverable strong attempt state and can publish it atomically.
 * Recovery routing therefore has to notice any occupied staged node before a
 * transaction-id journal is interpreted as legacy. This probe intentionally
 * does not decode, rename or delete the node. Invalid/symlink/non-regular nodes
 * remain evidence and are handled fail-closed by the authorized store path.
 */
class PartyQuestRuntimeRestoreAttemptPendingPublicationProbe final
{
public:
    [[nodiscard]] static PartyQuestRuntimeRestoreAttemptPendingPublicationStatus Probe(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId,
        uint64_t aTransactionId) noexcept
    {
        if (!acCampaignId.IsValid() ||
            !acPlayerProfileId.IsValid() ||
            aTransactionId == 0 ||
            !PartyQuestCoopSaveLayout::Matches(
                acPaths,
                acCampaignId,
                acPlayerProfileId))
        {
            return PartyQuestRuntimeRestoreAttemptPendingPublicationStatus::ProbeFailed;
        }

        const auto primary = PartyQuestRuntimeRestoreAttemptStore::GetStatePath(
            acPaths,
            aTransactionId);
        if (primary.empty())
            return PartyQuestRuntimeRestoreAttemptPendingPublicationStatus::ProbeFailed;

        std::filesystem::path temporary;
        try
        {
            temporary = primary;
            temporary += ".tmp";
        }
        catch (...)
        {
            return PartyQuestRuntimeRestoreAttemptPendingPublicationStatus::ProbeFailed;
        }

        if (!PartyQuestDurableResourcePolicy::IsFilesystemPathWithinBudget(temporary))
            return PartyQuestRuntimeRestoreAttemptPendingPublicationStatus::ProbeFailed;

        try
        {
            std::error_code ec;
            const auto node = std::filesystem::symlink_status(temporary, ec);
            if (node.type() == std::filesystem::file_type::not_found ||
                ec == std::errc::no_such_file_or_directory ||
                ec == std::errc::not_a_directory)
            {
                return PartyQuestRuntimeRestoreAttemptPendingPublicationStatus::Missing;
            }
            if (ec)
                return PartyQuestRuntimeRestoreAttemptPendingPublicationStatus::ProbeFailed;

            return PartyQuestRuntimeRestoreAttemptPendingPublicationStatus::Present;
        }
        catch (...)
        {
            return PartyQuestRuntimeRestoreAttemptPendingPublicationStatus::ProbeFailed;
        }
    }
};
