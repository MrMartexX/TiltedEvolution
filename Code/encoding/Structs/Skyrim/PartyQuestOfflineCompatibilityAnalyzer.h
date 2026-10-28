#pragma once

#include <Structs/Skyrim/PartyQuestCompatibilityEnvironmentCache.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

enum class PartyQuestOfflineAnalysisStatus : uint8_t
{
    Complete,
    Incomplete,
    Cancelled
};

enum class PartyQuestOfflineTransitionDisposition : uint8_t
{
    SafeCandidate,
    NeedsReview,
    Unsupported
};

struct PartyQuestOfflineArchiveSource
{
    std::filesystem::path RelativePath;
    // Later entries have higher resource priority. Loose files always win.
    uint32_t Priority{};
};

struct PartyQuestOfflineQuestRequest
{
    std::string SourcePlugin;
    uint32_t LocalFormId{};
};

struct PartyQuestOfflineAnalyzerInput
{
    static constexpr uint32_t SchemaVersion = 1;

    uint32_t AnalyzerVersion{1};
    std::string RuntimeVersion;
    std::string RuntimeExecutableSha256;
    PartyQuestCompatibilityEnvironmentSnapshot Environment;
    std::vector<PartyQuestOfflineArchiveSource> OrderedArchives;
    // Complete, explicit set of loose Papyrus sources in the stable snapshot.
    std::vector<std::filesystem::path> LoosePexFiles;
    std::vector<PartyQuestOfflineQuestRequest> Quests;
};

struct PartyQuestOfflineTransitionEdge
{
    uint16_t FromStage{};
    uint16_t ToStage{};
    PartyQuestOfflineTransitionDisposition Disposition{
        PartyQuestOfflineTransitionDisposition::NeedsReview};
    std::string Reason;
};

struct PartyQuestOfflineQuestCandidate
{
    std::string SourcePlugin;
    std::string WinningPlugin;
    uint32_t LocalFormId{};
    bool SourceIsLite{};
    std::string EditorId;
    uint64_t ResolvedTopologyFingerprint{};
    uint64_t WinningOverrideFingerprint{};
    uint64_t ScriptDependencyFingerprint{};
    std::vector<uint16_t> Stages;
    std::vector<uint16_t> Objectives;
    std::vector<std::string> ScriptDependencies;
    uint32_t AliasCount{};
    uint32_t SceneCount{};
    bool HasPapyrusFragments{};
    bool HasConditions{};
    bool HasAliases{};
    bool HasScenes{};
    bool HasPotentialSideEffects{};
    std::vector<PartyQuestOfflineTransitionEdge> Transitions;
};

struct PartyQuestOfflineAnalyzerResult
{
    PartyQuestOfflineAnalysisStatus Status{
        PartyQuestOfflineAnalysisStatus::Incomplete};
    uint32_t SchemaVersion{PartyQuestOfflineAnalyzerInput::SchemaVersion};
    uint32_t AnalyzerVersion{};
    PartyQuestCompatibilityEnvironmentFingerprints EnvironmentFingerprints;
    uint64_t InputProvenanceFingerprint{};
    std::vector<PartyQuestOfflineQuestCandidate> Quests;
    std::vector<std::string> Errors;
    std::string MachineReadableManifest;
    std::string HumanReviewReport;

    [[nodiscard]] bool IsComplete() const noexcept
    {
        return Status == PartyQuestOfflineAnalysisStatus::Complete &&
            Errors.empty() && EnvironmentFingerprints.IsValid();
    }
};

/**
 * Deterministic, read-only review-input generator. It never edits the reviewed
 * profile registry and its result carries no runtime or mutation authority.
 */
class PartyQuestOfflineCompatibilityAnalyzer final
{
public:
    [[nodiscard]] static PartyQuestOfflineAnalyzerResult Analyze(
        const PartyQuestOfflineAnalyzerInput& acInput,
        std::stop_token aStopToken = {}) noexcept;

    /** Atomic pair publication; failures leave no partial final artifact. */
    [[nodiscard]] static bool WriteArtifacts(
        const PartyQuestOfflineAnalyzerResult& acResult,
        const std::filesystem::path& acManifestPath,
        const std::filesystem::path& acReportPath) noexcept;
};
