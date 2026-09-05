#include <TiltedOnlinePCH.h>

#include <PartyQuestSkyrimRuntimeCompatibilityEvidence.h>

#include <Services/QuestSnapshotCollector.h>
#include <Systems/ModSystem.h>

#include <Forms/BGSBaseAlias.h>
#include <Forms/TESQuest.h>
#include <Games/TES.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string>

namespace
{
class CompatibilityHash final
{
public:
    void Mix(const void* apData, size_t aSize) noexcept
    {
        const auto* p = static_cast<const uint8_t*>(apData);
        for (size_t i = 0; i < aSize; ++i)
        {
            m_value ^= p[i];
            m_value *= 1099511628211ull;
        }
    }

    template <class T>
    void MixPod(const T& acValue) noexcept
    {
        static_assert(std::is_trivially_copyable_v<T>);
        Mix(&acValue, sizeof(acValue));
    }

    void MixString(std::string aValue) noexcept
    {
        std::transform(
            aValue.begin(),
            aValue.end(),
            aValue.begin(),
            [](unsigned char aCharacter)
            {
                return static_cast<char>(std::tolower(aCharacter));
            });
        const uint64_t size = static_cast<uint64_t>(aValue.size());
        MixPod(size);
        if (!aValue.empty())
            Mix(aValue.data(), aValue.size());
    }

    [[nodiscard]] uint64_t Value() const noexcept
    {
        return m_value == 0 ? 1 : m_value;
    }

private:
    uint64_t m_value{14695981039346656037ull};
};

std::optional<std::filesystem::path> GetSkyrimDataDirectory() noexcept
{
#ifdef _WIN32
    try
    {
        std::array<wchar_t, 32768> path{};
        const DWORD length = GetModuleFileNameW(
            nullptr,
            path.data(),
            static_cast<DWORD>(path.size()));
        if (length == 0 || length >= path.size())
            return std::nullopt;

        std::filesystem::path executable(
            std::wstring_view(path.data(), static_cast<size_t>(length)));
        const auto data = executable.parent_path() / L"Data";
        std::error_code ec;
        if (!std::filesystem::is_directory(data, ec) || ec)
            return std::nullopt;
        return data.lexically_normal();
    }
    catch (...)
    {
        return std::nullopt;
    }
#else
    return std::nullopt;
#endif
}

uint64_t HashResolvedQuestTopology(
    TESQuest* apQuest,
    const ModSystem& acModSystem,
    const GameId& acExpectedQuestId) noexcept
{
    if (!apQuest || !acExpectedQuestId)
        return 0;

    try
    {
        GameId observedQuestId;
        if (!acModSystem.GetServerModId(apQuest->formID, observedQuestId) ||
            observedQuestId != acExpectedQuestId)
        {
            return 0;
        }

        CompatibilityHash hash;
        constexpr uint64_t domain = 0x52534C5651554553ull; // "RSLVQUES"
        hash.MixPod(domain);
        hash.MixPod(observedQuestId.ModId);
        hash.MixPod(observedQuestId.BaseId);
        hash.MixPod(apQuest->type);
        hash.MixPod(apQuest->priority);

        // Exclude runtime state flags (Enabled/Completed/Failed/StageWait/Active).
        // The remaining bits describe static quest behavior relevant to adapter
        // compatibility rather than the current playthrough state.
        constexpr uint16_t kRuntimeFlags =
            TESQuest::Flags::Enabled |
            TESQuest::Flags::Completed |
            TESQuest::Flags::Failed |
            TESQuest::Flags::StageWait |
            TESQuest::Flags::Active;
        const uint16_t staticFlags = apQuest->flags & ~kRuntimeFlags;
        hash.MixPod(staticFlags);
        hash.MixString(apQuest->idName.AsAscii());

        const uint64_t stageCount = static_cast<uint64_t>(apQuest->stages.Size());
        hash.MixPod(stageCount);
        for (auto* pStage : apQuest->stages)
        {
            if (!pStage)
                return 0;
            hash.MixPod(pStage->stageIndex);
        }

        const uint64_t objectiveCount =
            static_cast<uint64_t>(apQuest->objectives.Size());
        hash.MixPod(objectiveCount);
        for (auto* pObjective : apQuest->objectives)
        {
            if (!pObjective)
                return 0;
            hash.MixPod(pObjective->stageId);
        }

        const uint64_t aliasCount = static_cast<uint64_t>(apQuest->aliases.length);
        hash.MixPod(aliasCount);
        for (auto* pAlias : apQuest->aliases)
        {
            if (!pAlias)
                return 0;
            hash.MixPod(pAlias->aliasId);
            hash.MixPod(pAlias->flags);
            hash.MixPod(pAlias->fillType);
            hash.MixString(pAlias->aliasName.AsAscii());
            hash.MixString(pAlias->QType().AsAscii());
        }

        const uint64_t sceneCount = static_cast<uint64_t>(apQuest->scenes.length);
        hash.MixPod(sceneCount);
        for (auto* pScene : apQuest->scenes)
        {
            if (!pScene)
                return 0;
            GameId sceneId;
            if (!acModSystem.GetServerModId(pScene->formID, sceneId))
                return 0;
            hash.MixPod(sceneId.ModId);
            hash.MixPod(sceneId.BaseId);
            const uint64_t actorCount = static_cast<uint64_t>(pScene->actorIds.length);
            hash.MixPod(actorCount);
            for (const uint32_t actorId : pScene->actorIds)
                hash.MixPod(actorId);
            const uint64_t phaseCount = static_cast<uint64_t>(pScene->phases.length);
            hash.MixPod(phaseCount);
        }

        return hash.Value();
    }
    catch (...)
    {
        return 0;
    }
}

uint64_t CombineWinningOverrideFingerprint(
    uint64_t aResolvedRecordFingerprint,
    uint64_t aPluginEnvironmentFingerprint) noexcept
{
    if (aResolvedRecordFingerprint == 0 || aPluginEnvironmentFingerprint == 0)
        return 0;

    CompatibilityHash hash;
    constexpr uint64_t domain = 0x57494E4F56524445ull; // "WINOVRDE"
    hash.MixPod(domain);
    hash.MixPod(aResolvedRecordFingerprint);
    hash.MixPod(aPluginEnvironmentFingerprint);
    return hash.Value();
}

// No production quest is authorized merely because its local observations are
// internally self-consistent. Entries are added here only after a concrete
// quest/version profile has been reviewed against a reference runtime and the
// exact expected fingerprints have been recorded in source review.
constexpr std::array<PartyQuestRuntimeCompatibilityRequirement, 0>
    kReviewedProfiles{};
} // namespace

PartyQuestRuntimeCompatibilityManifest
PartyQuestSkyrimRuntimeCompatibilityEvidence::BuildReviewedManifest() noexcept
{
    PartyQuestRuntimeCompatibilityManifest manifest;
    for (const auto& profile : kReviewedProfiles)
    {
        if (!manifest.AddRequirement(profile))
            return {};
    }
    return manifest;
}

bool PartyQuestSkyrimRuntimeCompatibilityEvidence::HasReviewedProfile(
    const GameId& acQuestId) noexcept
{
    for (const auto& profile : kReviewedProfiles)
    {
        if (profile.QuestId == acQuestId)
            return true;
    }
    return false;
}

std::optional<PartyQuestCompatibilityEnvironmentSnapshot>
PartyQuestSkyrimRuntimeCompatibilityEvidence::CaptureEnvironmentSnapshot() noexcept
{
    try
    {
        const auto dataDirectory = GetSkyrimDataDirectory();
        auto* pManager = ModManager::Get();
        if (!dataDirectory || !pManager)
            return std::nullopt;

        PartyQuestCompatibilityEnvironmentSnapshot snapshot;
        snapshot.DataDirectory = *dataDirectory;
        for (auto* pMod : pManager->mods)
        {
            if (!pMod || !pMod->IsLoaded())
                continue;
            std::string filename(pMod->filename);
            if (filename.empty())
                return std::nullopt;
            snapshot.OrderedPlugins.push_back(
                {std::filesystem::path(std::move(filename)), pMod->IsLite()});
        }
        if (snapshot.OrderedPlugins.empty())
            return std::nullopt;
        return snapshot;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<PartyQuestRuntimeCompatibilityFacts>
PartyQuestSkyrimRuntimeCompatibilityEvidence::ObserveDiagnostic(
    TESQuest* apQuest,
    const ModSystem& acModSystem,
    const GameId& acExpectedQuestId,
    const PartyQuestCompatibilityEnvironmentFingerprints& acEnvironment) noexcept
{
    if (!apQuest || !acExpectedQuestId || !acEnvironment.IsValid())
        return std::nullopt;

    const uint64_t resolvedRecord = HashResolvedQuestTopology(
        apQuest,
        acModSystem,
        acExpectedQuestId);
    const uint64_t winningOverride = CombineWinningOverrideFingerprint(
        resolvedRecord,
        acEnvironment.PluginEnvironment);
    if (resolvedRecord == 0 || winningOverride == 0)
        return std::nullopt;

    PartyQuestRuntimeCompatibilityFacts facts;
    facts.ProfileVersion = ProfileVersion;
    facts.ResolvedRecordFingerprint = resolvedRecord;
    facts.WinningOverrideFingerprint = winningOverride;
    facts.ScriptFingerprint = acEnvironment.ScriptEnvironment;
    facts.NativeAdapterFingerprint = NativeAdapterFingerprint;
    facts.AdapterMutationComponents =
        PartyQuestVerificationComponent::QuestSnapshot;
    return facts;
}

std::optional<PartyQuestRuntimeProcessPlanningEvidence>
PartyQuestSkyrimRuntimeCompatibilityEvidence::ObserveFresh(
    TESQuest* apQuest,
    const ModSystem& acModSystem,
    const PartyQuestRuntimeCanonicalCandidate& acCandidate,
    const PartyQuestCompatibilityEnvironmentFingerprints& acEnvironment) noexcept
{
    if (!apQuest || !acCandidate.CampaignId.IsValid() ||
        acCandidate.TransactionId == 0 || acCandidate.WorldRevision == 0 ||
        !acCandidate.CanonicalSnapshot.QuestId ||
        !HasReviewedProfile(acCandidate.CanonicalSnapshot.QuestId))
    {
        return std::nullopt;
    }

    const auto compatibility = ObserveDiagnostic(
        apQuest,
        acModSystem,
        acCandidate.CanonicalSnapshot.QuestId,
        acEnvironment);
    if (!compatibility)
        return std::nullopt;

    PartyQuestRuntimeProcessPlanningEvidence evidence;
    evidence.SyncFacts = QuestSnapshotCollector::CollectSyncFacts(apQuest);
    evidence.CompatibilityFacts = *compatibility;

    // The current narrow stage adapter has no external checkpoint sidecar
    // capability. The explicit empty manifest is still an exact, non-zero
    // transaction contract; core .ess/.skse capture is handled by PreRepair and
    // is not an external sidecar capability.
    if (evidence.SidecarManifest.ComputeFingerprint() == 0)
        return std::nullopt;

    return evidence;
}
