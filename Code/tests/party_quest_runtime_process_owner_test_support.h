#pragma once

#include <Structs/Skyrim/PartyQuestCoopSaveLayout.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>

#include <party_quest_runtime_session_owner_test_access.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <string>

class PartyQuestRuntimeProcessOwnerTestScope final
{
public:
    PartyQuestRuntimeProcessOwnerTestScope(
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId,
        const std::string& acPrefix)
        : m_owner(PartyQuestRuntimeSessionOwner::GetProcessOwner())
        , m_ownsRoot(true)
    {
        PartyQuestRuntimeSessionOwnerTestAccess::ForceClearProcessOwner();

        const auto nonce = std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count();
        m_root = std::filesystem::temp_directory_path() /
            (acPrefix + "_" + std::to_string(nonce));

        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
        ec.clear();
        std::filesystem::create_directories(m_root, ec);
        REQUIRE_FALSE(ec);

        const auto paths = PartyQuestCoopSaveLayout::Build(
            m_root / "CoopCampaigns",
            acCampaignId,
            acPlayerProfileId);
        REQUIRE(paths.has_value());
        m_paths = *paths;
        Bind(acCampaignId, acPlayerProfileId);
    }

    PartyQuestRuntimeProcessOwnerTestScope(
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId,
        const PartyQuestCoopSavePaths& acPaths)
        : m_owner(PartyQuestRuntimeSessionOwner::GetProcessOwner())
        , m_paths(acPaths)
    {
        PartyQuestRuntimeSessionOwnerTestAccess::ForceClearProcessOwner();
        Bind(acCampaignId, acPlayerProfileId);
    }

    ~PartyQuestRuntimeProcessOwnerTestScope()
    {
        PartyQuestRuntimeSessionOwnerTestAccess::ForceClearProcessOwner();
        if (m_ownsRoot && !m_root.empty())
        {
            std::error_code ec;
            std::filesystem::remove_all(m_root, ec);
        }
    }

    PartyQuestRuntimeProcessOwnerTestScope(
        const PartyQuestRuntimeProcessOwnerTestScope&) = delete;
    PartyQuestRuntimeProcessOwnerTestScope& operator=(
        const PartyQuestRuntimeProcessOwnerTestScope&) = delete;

    [[nodiscard]] PartyQuestRuntimeGuardedSession& GuardedSession() const
    {
        auto* guarded = m_owner.GetGuardedSession();
        REQUIRE(guarded != nullptr);
        return *guarded;
    }

    [[nodiscard]] PartyQuestRuntimeApplySession& RuntimeSession() const
    {
        const auto* session = m_owner.GetRuntimeSession();
        REQUIRE(session != nullptr);
        return const_cast<PartyQuestRuntimeApplySession&>(*session);
    }

    [[nodiscard]] const PartyQuestCoopSavePaths& Paths() const noexcept
    {
        return m_paths;
    }

private:
    void Bind(
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId)
    {
        const auto bound = m_owner.Bind(
            acCampaignId,
            acPlayerProfileId,
            m_paths);
        REQUIRE(bound.IsBound());
        REQUIRE(m_owner.IsBound());
        REQUIRE(m_owner.GetGuardedSession() != nullptr);
        REQUIRE(m_owner.GetRuntimeSession() != nullptr);
        REQUIRE_FALSE(PartyQuestSaveGuard::GetProcessGuard().IsActive());
    }

    PartyQuestRuntimeSessionOwner& m_owner;
    std::filesystem::path m_root;
    PartyQuestCoopSavePaths m_paths;
    bool m_ownsRoot{};
};
