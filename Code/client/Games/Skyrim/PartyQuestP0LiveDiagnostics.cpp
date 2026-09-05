#include <TiltedOnlinePCH.h>

#include <PartyQuestP0LiveDiagnostics.h>
#include <PartyQuestSkyrimPapyrusRuntimeObserver.h>
#include <PartyQuestSkyrimRuntimeCompatibilityEvidence.h>
#include <SaveLoad.h>

#include <Systems/ModSystem.h>
#include <Services/QuestSnapshotCollector.h>
#include <VersionDb.h>

#include <Forms/TESQuest.h>

#include <Structs/Skyrim/PartyQuestAdmission.h>
#include <Structs/Skyrim/PartyQuestExceptionBoundary.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeLifecycleIntegration.h>
#include <Structs/Skyrim/PartyQuestRuntimeSafety.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace
{
std::mutex s_mutex;
std::ofstream s_stream;
std::atomic<uint64_t> s_sequence{0};
std::atomic_bool s_initialized{false};
std::atomic_bool s_enabled{false};

template <class TCallable>
void RunDiagnostic(TCallable&& aCallable) noexcept
{
    (void)PartyQuestExceptionBoundary::Invoke(
        std::forward<TCallable>(aCallable));
}

std::string EscapeJson(const char* apText)
{
    if (!apText)
        return {};

    std::ostringstream out;
    for (const unsigned char ch : std::string(apText))
    {
        switch (ch)
        {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20)
            {
                out << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<uint32_t>(ch) << std::dec;
            }
            else
            {
                out << static_cast<char>(ch);
            }
            break;
        }
    }
    return out.str();
}

bool ParseBool(const std::string& acValue) noexcept
{
    return PartyQuestExceptionBoundary::InvokeOr<bool>(
        false,
        [&]()
        {
            std::string value;
            value.reserve(acValue.size());
            for (const char ch : acValue)
            {
                if (!std::isspace(static_cast<unsigned char>(ch)))
                {
                    value.push_back(static_cast<char>(
                        std::tolower(static_cast<unsigned char>(ch))));
                }
            }

            return value == "1" || value == "true" || value == "yes" ||
                value == "on";
        });
}

bool ReadEnabledSetting() noexcept
{
    if (const char* pEnvironment = std::getenv("STR_PARTY_QUEST_P0_LIVE_DIAGNOSTICS"))
        return ParseBool(pEnvironment);

    try
    {
        const auto path = TiltedPhoques::GetPath() / "party_quest_p0_live.ini";
        std::ifstream stream(path);
        if (!stream)
            return false;

        bool gameplaySection = false;
        std::string line;
        while (std::getline(stream, line))
        {
            const auto first = line.find_first_not_of(" \t\r\n");
            if (first == std::string::npos || line[first] == ';' || line[first] == '#')
                continue;

            if (line[first] == '[')
            {
                const auto close = line.find(']', first + 1);
                gameplaySection = close != std::string::npos &&
                    line.substr(first + 1, close - first - 1) == "Gameplay";
                continue;
            }

            if (!gameplaySection)
                continue;

            const auto equals = line.find('=', first);
            if (equals == std::string::npos)
                continue;

            auto key = line.substr(first, equals - first);
            while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back())))
                key.pop_back();

            if (key == "bEnablePartyQuestP0LiveDiagnostics")
                return ParseBool(line.substr(equals + 1));
        }
    }
    catch (...)
    {
        return false;
    }

    return false;
}

void WriteEvent(const char* acEvent, const std::string& acFields = {}) noexcept
{
    if (!s_enabled.load(std::memory_order_acquire))
        return;

    try
    {
        const uint64_t sequence = s_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
        const uint32_t threadId = GetCurrentThreadId();
        const uint64_t unixMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

        std::scoped_lock lock(s_mutex);
        if (!s_stream)
            return;

        s_stream << "{\"seq\":" << sequence
                 << ",\"unix_ms\":" << unixMs
                 << ",\"event\":\"" << EscapeJson(acEvent) << "\""
                 << ",\"thread_id\":" << threadId;
        if (!acFields.empty())
            s_stream << ',' << acFields;
        s_stream << "}\n";
        s_stream.flush();
    }
    catch (...)
    {
        // Diagnostics must never perturb the game or mutation safety state.
    }
}

std::optional<std::array<uint32_t, 4>> GetExecutableVersion() noexcept
{
    return PartyQuestExceptionBoundary::InvokeOr<
        std::optional<std::array<uint32_t, 4>>>(
        std::nullopt,
        []() -> std::optional<std::array<uint32_t, 4>>
        {
            wchar_t path[MAX_PATH]{};
            const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
            if (length == 0 || length >= MAX_PATH)
                return std::nullopt;

            DWORD ignored = 0;
            const DWORD size = GetFileVersionInfoSizeW(path, &ignored);
            if (size == 0)
                return std::nullopt;

            std::vector<std::byte> data(size);
            if (!GetFileVersionInfoW(path, 0, size, data.data()))
                return std::nullopt;

            VS_FIXEDFILEINFO* pInfo = nullptr;
            UINT infoSize = 0;
            if (!VerQueryValueW(
                    data.data(),
                    L"\\",
                    reinterpret_cast<void**>(&pInfo),
                    &infoSize) ||
                !pInfo || infoSize < sizeof(VS_FIXEDFILEINFO))
            {
                return std::nullopt;
            }

            return std::array<uint32_t, 4>{
                HIWORD(pInfo->dwFileVersionMS),
                LOWORD(pInfo->dwFileVersionMS),
                HIWORD(pInfo->dwFileVersionLS),
                LOWORD(pInfo->dwFileVersionLS)};
        });
}

std::string VersionJson(const std::array<uint32_t, 4>& acVersion)
{
    std::ostringstream out;
    out << '[' << acVersion[0] << ',' << acVersion[1] << ','
        << acVersion[2] << ',' << acVersion[3] << ']';
    return out.str();
}

std::string GameIdJson(const GameId& acId)
{
    std::ostringstream out;
    out << "{\"mod_id\":" << acId.ModId << ",\"base_id\":" << acId.BaseId << '}';
    return out.str();
}

const char* StatusName(QuestSnapshotStatus aStatus) noexcept
{
    switch (aStatus)
    {
    case QuestSnapshotStatus::Inactive: return "inactive";
    case QuestSnapshotStatus::Running: return "running";
    case QuestSnapshotStatus::Stopped: return "stopped";
    case QuestSnapshotStatus::Completed: return "completed";
    case QuestSnapshotStatus::Failed: return "failed";
    default: return "unknown";
    }
}

const char* SyncClassName(PartyQuestSyncClass aClass) noexcept
{
    switch (aClass)
    {
    case PartyQuestSyncClass::SharedCandidate: return "shared-candidate";
    case PartyQuestSyncClass::ServiceCandidate: return "service-candidate";
    case PartyQuestSyncClass::LocalOnly: return "local-only";
    default: return "unknown";
    }
}

const char* AdmissionName(PartyQuestAdmissionStatus aStatus) noexcept
{
    switch (aStatus)
    {
    case PartyQuestAdmissionStatus::SharedProvisional: return "shared-provisional";
    case PartyQuestAdmissionStatus::BlockedServiceCandidate: return "blocked-service-candidate";
    case PartyQuestAdmissionStatus::BlockedLocalOnly: return "blocked-local-only";
    case PartyQuestAdmissionStatus::BlockedConfirmedServiceQuest: return "blocked-confirmed-service-quest";
    default: return "unknown";
    }
}

const char* SafetyName(PartyQuestRuntimeSafetyStatus aStatus) noexcept
{
    switch (aStatus)
    {
    case PartyQuestRuntimeSafetyStatus::Blocked: return "blocked";
    case PartyQuestRuntimeSafetyStatus::StageOnly: return "stage-only";
    case PartyQuestRuntimeSafetyStatus::Deferred: return "deferred";
    case PartyQuestRuntimeSafetyStatus::RequiresAdapter: return "requires-adapter";
    case PartyQuestRuntimeSafetyStatus::RuntimeSafe: return "runtime-safe";
    default: return "unknown";
    }
}

const char* SafetyReasonName(PartyQuestRuntimeSafetyReason aReason) noexcept
{
    switch (aReason)
    {
    case PartyQuestRuntimeSafetyReason::AdmissionBlocked: return "admission-blocked";
    case PartyQuestRuntimeSafetyReason::SimpleStageTransition: return "simple-stage-transition";
    case PartyQuestRuntimeSafetyReason::ReferenceAliasesNeedWorld: return "reference-aliases-need-world";
    case PartyQuestRuntimeSafetyReason::SceneParticipantActive: return "scene-participant-active";
    case PartyQuestRuntimeSafetyReason::InactiveQuestState: return "inactive-quest-state";
    case PartyQuestRuntimeSafetyReason::TerminalQuestState: return "terminal-quest-state";
    case PartyQuestRuntimeSafetyReason::CreatedReferences: return "created-references";
    case PartyQuestRuntimeSafetyReason::LocationAliases: return "location-aliases";
    case PartyQuestRuntimeSafetyReason::QuestObjectAliases: return "quest-object-aliases";
    case PartyQuestRuntimeSafetyReason::UnresolvedReferenceAliases: return "unresolved-reference-aliases";
    case PartyQuestRuntimeSafetyReason::ComplexAliasTopology: return "complex-alias-topology";
    case PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter: return "verified-native-adapter";
    default: return "unknown";
    }
}

const char* PapyrusObservationStatusName(
    PartyQuestPapyrusRuntimeObservationStatus aStatus) noexcept
{
    switch (aStatus)
    {
    case PartyQuestPapyrusRuntimeObservationStatus::Unsupported:
        return "unsupported";
    case PartyQuestPapyrusRuntimeObservationStatus::Unknown:
        return "unknown";
    case PartyQuestPapyrusRuntimeObservationStatus::Busy:
        return "busy";
    case PartyQuestPapyrusRuntimeObservationStatus::Idle:
        return "idle";
    }
    return "unknown";
}
} // namespace

void PartyQuestP0LiveDiagnostics::Initialize() noexcept
{
    bool expected = false;
    if (!s_initialized.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return;

    if (!ReadEnabledSetting())
        return;

    try
    {
        const auto logPath = TiltedPhoques::GetPath() / "logs";
        std::error_code ec;
        create_directory(logPath, ec);
        s_stream.open(logPath / "party_quest_p0_live.jsonl", std::ios::out | std::ios::trunc);
        if (!s_stream)
            return;

        s_enabled.store(true, std::memory_order_release);

        std::ostringstream startup;
        startup << "\"generation\":"
                << PartyQuestRuntimeGenerationFence::GetProcessFence().GetGeneration()
                << ",\"build_commit\":\"" << EscapeJson(BUILD_COMMIT) << "\"";

        if (const auto executableVersion = GetExecutableVersion())
            startup << ",\"executable_version\":" << VersionJson(*executableVersion);
        else
            startup << ",\"executable_version\":{\"available\":false,\"reason\":\"win32-file-version-unavailable\"}";

        const auto& versionDb = VersionDb::Get();
        if (versionDb.IsLoaded())
        {
            int major = 0;
            int minor = 0;
            int patch = 0;
            int build = 0;
            versionDb.GetLoadedVersion(major, minor, patch, build);
            const std::array<uint32_t, 4> dbVersion{
                static_cast<uint32_t>(std::max(major, 0)),
                static_cast<uint32_t>(std::max(minor, 0)),
                static_cast<uint32_t>(std::max(patch, 0)),
                static_cast<uint32_t>(std::max(build, 0))};
            startup << ",\"version_db\":" << VersionJson(dbVersion);
            const char* idNamespace = "unknown";
            switch (versionDb.GetLoadedIdNamespace())
            {
            case SkyrimAddressLibraryIdNamespace::Se:
                idNamespace = "se";
                break;
            case SkyrimAddressLibraryIdNamespace::Ae:
                idNamespace = "ae";
                break;
            case SkyrimAddressLibraryIdNamespace::Unknown:
                break;
            }
            startup << ",\"address_library\":{\"loaded\":true"
                    << ",\"format\":"
                    << versionDb.GetLoadedDatabaseFormat()
                    << ",\"id_namespace\":\"" << idNamespace << "\""
                    << ",\"entry_count\":"
                    << versionDb.GetLoadedEntryCount()
                    << ",\"module_name\":\""
                    << EscapeJson(versionDb.GetModuleName().c_str()) << "\"}";
            if (const auto executableVersion = GetExecutableVersion())
                startup << ",\"executable_version_matches_version_db\":"
                        << (dbVersion == *executableVersion ? "true" : "false");
        }
        else
        {
            startup << ",\"version_db\":{\"available\":false,\"reason\":\"version-db-not-loaded\"}"
                    << ",\"address_library\":{\"loaded\":false,\"reason\":\"version-db-not-loaded\"}";
        }

        startup << ",\"runtime_profile\":{\"approved\":false,\"reason\":\"live-version-bound-papyrus-adapter-proof-pending\"}"
                << ",\"papyrus_generation_authority\":{\"available\":false,\"reason\":\"diagnostic-ingress-source-does-not-issue-authority\"}"
                << ",\"papyrus_snapshot_authority\":{\"available\":false,\"reason\":\"diagnostic-coherent-sampler-does-not-issue-authority\"}"
                << ",\"compatibility_observation\":{\"available\":false,\"reason\":\"quest-scoped-observation-pending\"}"
                << ",\"canonical_set_stage_enabled\":false";
        WriteEvent("run_start", startup.str());

        RecordLifecycleCapabilities("startup");
    }
    catch (...)
    {
        s_enabled.store(false, std::memory_order_release);
    }
}

bool PartyQuestP0LiveDiagnostics::IsEnabled() noexcept
{
    return s_enabled.load(std::memory_order_acquire);
}

void PartyQuestP0LiveDiagnostics::RecordRuntimeThreadObservation(
    bool aAccepted,
    uint32_t aBoundThreadId,
    uint32_t aCurrentThreadId) noexcept
{
    RunDiagnostic(
        [&]()
        {
            std::ostringstream fields;
            fields << "\"accepted\":" << (aAccepted ? "true" : "false")
                   << ",\"bound_thread_id\":" << aBoundThreadId
                   << ",\"current_thread_id\":" << aCurrentThreadId;
            WriteEvent(
                aAccepted ? "runtime_thread_observed"
                          : "runtime_thread_migration_rejected",
                fields.str());
        });
}

void PartyQuestP0LiveDiagnostics::RecordGenerationTransition(
    const char* acReason,
    const char* acPhase,
    uint64_t aGenerationBefore,
    uint64_t aGenerationAfter) noexcept
{
    RunDiagnostic(
        [&]()
        {
            std::ostringstream fields;
            fields << "\"reason\":\"" << EscapeJson(acReason) << "\""
                   << ",\"phase\":\"" << EscapeJson(acPhase) << "\""
                   << ",\"generation_before\":" << aGenerationBefore
                   << ",\"generation_after\":" << aGenerationAfter;
            WriteEvent("generation_transition", fields.str());
        });
}

void PartyQuestP0LiveDiagnostics::RecordTransportState(const char* acState) noexcept
{
    RunDiagnostic(
        [&]()
        {
            std::ostringstream fields;
            fields << "\"state\":\"" << EscapeJson(acState) << "\""
                   << ",\"generation\":"
                   << PartyQuestRuntimeGenerationFence::GetProcessFence().GetGeneration();
            WriteEvent("transport_state", fields.str());
        });
}

void PartyQuestP0LiveDiagnostics::RecordGamePresence(bool aInGame) noexcept
{
    RunDiagnostic(
        [&]()
        {
            std::ostringstream fields;
            fields << "\"in_game\":" << (aInGame ? "true" : "false")
                   << ",\"semantic\":\"overlay-player-presence-only-not-engine-lifecycle-authority\""
                   << ",\"generation\":"
                   << PartyQuestRuntimeGenerationFence::GetProcessFence().GetGeneration();
            WriteEvent("game_presence", fields.str());
        });
}

void PartyQuestP0LiveDiagnostics::RecordEngineSave(
    const char* acPhase,
    const char* acFileName,
    uint64_t aTransactionId,
    int32_t aDeviceId,
    uint32_t aOutputStats,
    bool aPermitted,
    bool aResultKnown,
    bool aResult) noexcept
{
    RunDiagnostic(
        [&]()
        {
            std::ostringstream fields;
            fields << "\"phase\":\"" << EscapeJson(acPhase) << "\""
                   << ",\"save_name\":\""
                   << EscapeJson(acFileName ? acFileName : "") << "\""
                   << ",\"transaction_id\":" << aTransactionId
                   << ",\"device_id\":" << aDeviceId
                   << ",\"output_stats\":" << aOutputStats
                   << ",\"permitted\":" << (aPermitted ? "true" : "false")
                   << ",\"generation\":"
                   << PartyQuestRuntimeGenerationFence::GetProcessFence().GetGeneration();
            if (aResultKnown)
                fields << ",\"result\":" << (aResult ? "true" : "false");
            else
                fields << ",\"result\":{\"available\":false,\"reason\":\"original-engine-call-not-returned-yet\"}";
            WriteEvent("skyrim_save_pipeline", fields.str());
        });
}

void PartyQuestP0LiveDiagnostics::RecordModMappingBegin(
    size_t aServerModCount,
    uint64_t aGenerationBefore,
    uint64_t aGenerationAfter) noexcept
{
    RunDiagnostic(
        [&]()
        {
            std::ostringstream fields;
            fields << "\"server_mod_count\":" << aServerModCount
                   << ",\"generation_before\":" << aGenerationBefore
                   << ",\"generation_after\":" << aGenerationAfter;
            WriteEvent("mod_mapping_rebuild_begin", fields.str());
        });
}

void PartyQuestP0LiveDiagnostics::RecordModMappingEntry(
    uint32_t aServerModId,
    const char* acFilename,
    bool aServerLite,
    bool aResolvedLocally,
    uint32_t aLocalModId,
    bool aLocalLite) noexcept
{
    RunDiagnostic(
        [&]()
        {
            std::ostringstream fields;
            fields << "\"server_mod_id\":" << aServerModId
                   << ",\"filename\":\"" << EscapeJson(acFilename) << "\""
                   << ",\"server_lite\":" << (aServerLite ? "true" : "false")
                   << ",\"resolved_locally\":"
                   << (aResolvedLocally ? "true" : "false");
            if (aResolvedLocally)
            {
                fields << ",\"local_mod_id\":" << aLocalModId
                       << ",\"local_lite\":" << (aLocalLite ? "true" : "false");
            }
            WriteEvent("mod_mapping_entry", fields.str());
        });
}

void PartyQuestP0LiveDiagnostics::RecordModMappingEnd(
    size_t aServerModCount,
    size_t aResolvedCount,
    size_t aMissingCount,
    uint64_t aGeneration) noexcept
{
    RunDiagnostic(
        [&]()
        {
            std::ostringstream fields;
            fields << "\"server_mod_count\":" << aServerModCount
                   << ",\"resolved_count\":" << aResolvedCount
                   << ",\"missing_count\":" << aMissingCount
                   << ",\"generation\":" << aGeneration;
            WriteEvent("mod_mapping_rebuild_end", fields.str());
        });
}

void PartyQuestP0LiveDiagnostics::RecordLifecycleCapabilities(
    const char* acPhase) noexcept
{
    RunDiagnostic(
        [&]()
        {
            const bool loadGame =
                PartyQuestRuntimeLifecycleIntegrationPolicy::HasVerifiedPreTransitionHook(
                    PartyQuestRuntimeLifecycleEvent::LoadGame);
            const bool newGame =
                PartyQuestRuntimeLifecycleIntegrationPolicy::HasVerifiedPreTransitionHook(
                    PartyQuestRuntimeLifecycleEvent::NewGame);
            const bool mainMenu =
                PartyQuestRuntimeLifecycleIntegrationPolicy::HasVerifiedPreTransitionHook(
                    PartyQuestRuntimeLifecycleEvent::MainMenu);
            const bool complete =
                PartyQuestRuntimeLifecycleIntegrationPolicy::
                    HasCompleteCharacterIdentityCoverage();

            std::ostringstream fields;
            fields << "\"phase\":\""
                   << EscapeJson(acPhase ? acPhase : "unknown") << "\""
                   << ",\"load_game_engine_hook\":{\"installed\":"
                   << (loadGame ? "true" : "false") << '}'
                   << ",\"load_game_completion_sink\":{\"installed\":"
                   << (IsPartyQuestLoadCompletionSinkInstalled() ? "true" : "false")
                   << '}'
                   << ",\"new_game_engine_hook\":{\"installed\":"
                   << (newGame ? "true" : "false") << '}'
                   << ",\"main_menu_engine_hook\":{\"installed\":"
                   << (mainMenu ? "true" : "false") << '}'
                   << ",\"complete_character_identity_coverage\":"
                   << (complete ? "true" : "false")
                   << ",\"save_engine_hook\":{\"installed\":"
                   << (IsPartyQuestSaveHookInstalled() ? "true" : "false")
                   << ",\"requires_observed_event_for_live_proof\":true}"
                   << ",\"profile_switch_engine_hook\":{\"installed\":false,"
                      "\"reason\":\"no-separate-profile-switch-boundary-approved\"}"
                   << ",\"grants_mutation_authority\":false";
            WriteEvent("lifecycle_capabilities", fields.str());
        });
}

void PartyQuestP0LiveDiagnostics::RecordPapyrusRuntimeObservation() noexcept
{
    if (!IsEnabled())
        return;

    RunDiagnostic(
        [&]()
        {
            // Capture a short startup sequence to prove repeated observations,
            // then a sparse heartbeat so a longer live run can show real ingress
            // transitions without generating an unbounded per-frame log.
            static uint64_t s_callCount = 0;
            static PartyQuestSkyrimPapyrusDiagnosticStatus s_lastStatus =
                PartyQuestSkyrimPapyrusDiagnosticStatus::VirtualMachineUnavailable;
            ++s_callCount;
            const bool startupWindow = s_callCount <= 24;
            const bool heartbeat = (s_callCount % 300) == 0;

            auto& observer =
                PartyQuestSkyrimPapyrusRuntimeObserver::GetProcessObserver();
            const auto sample = observer.SampleDiagnostics();
            const bool statusChanged = sample.DiagnosticStatus != s_lastStatus;
            s_lastStatus = sample.DiagnosticStatus;
            if (!startupWindow && !heartbeat && !statusChanged)
                return;

            const auto& observation = sample.Observation;
            const auto& counts = sample.Counts;
            std::ostringstream fields;
            fields << "\"sample_index\":" << s_callCount
                   << ",\"diagnostic_status\":\""
                   << PartyQuestSkyrimPapyrusRuntimeObserver::DiagnosticStatusName(
                          sample.DiagnosticStatus)
                   << "\""
                   << ",\"observation_status\":\""
                   << PapyrusObservationStatusName(observation.Status) << "\""
                   << ",\"pending_work_count\":" << observation.PendingWorkCount
                   << ",\"papyrus_generation\":"
                   << observation.QuestEventGeneration
                   << ",\"observed_work_domains\":"
                   << observation.ObservedWorkDomains
                   << ",\"domain_counts\":{"
                      "\"function_messages\":"
                   << counts.FunctionMessageQueues
                   << ",\"vm_tasks\":" << counts.VmTaskQueue
                   << ",\"ui_waiting\":" << counts.UiWaitingQueue
                   << ",\"suspend_resume\":" << counts.SuspendResumeQueues
                   << ",\"running_stacks\":" << counts.RunningStacks
                   << ",\"latent_returns\":" << counts.LatentReturnQueue << '}'
                   << ",\"ingress_hook_invocations\":"
                   << sample.IngressHookInvocationCount
                   << ",\"exact_runtime_identity\":"
                   << (sample.ExactRuntimeIdentity ? "true" : "false")
                   << ",\"ingress_hooks_registered\":"
                   << (sample.IngressHooksRegistered ? "true" : "false")
                   << ",\"virtual_table_matched\":"
                   << (sample.VirtualTableMatched ? "true" : "false")
                   << ",\"authoritative\":false"
                   << ",\"grants_mutation_authority\":false";
            WriteEvent("papyrus_runtime_observation", fields.str());
        });
}

void PartyQuestP0LiveDiagnostics::RecordCompatibilityObservation(
    TESQuest* apQuest,
    const GameId& acExpectedQuestId,
    const ModSystem& acModSystem,
    const PartyQuestCompatibilityEnvironmentFingerprints& acEnvironment) noexcept
{
    if (!apQuest || !acExpectedQuestId || !IsEnabled())
        return;

    RunDiagnostic(
        [&]()
        {
            const auto compatibility =
                PartyQuestSkyrimRuntimeCompatibilityEvidence::ObserveDiagnostic(
                    apQuest,
                    acModSystem,
                    acExpectedQuestId,
                    acEnvironment);

            std::ostringstream fields;
            fields << "\"generation\":"
                   << PartyQuestRuntimeGenerationFence::GetProcessFence().GetGeneration()
                   << ",\"quest_id\":" << GameIdJson(acExpectedQuestId);

            if (compatibility)
            {
                fields << ",\"available\":true"
                       << ",\"profile_version\":" << compatibility->ProfileVersion
                       << ",\"resolved_record_fingerprint\":"
                       << compatibility->ResolvedRecordFingerprint
                       << ",\"winning_override_fingerprint\":"
                       << compatibility->WinningOverrideFingerprint
                       << ",\"script_fingerprint\":"
                       << compatibility->ScriptFingerprint
                       << ",\"native_adapter_fingerprint\":"
                       << compatibility->NativeAdapterFingerprint
                       << ",\"adapter_mutation_components\":"
                       << static_cast<uint32_t>(
                              compatibility->AdapterMutationComponents)
                       << ",\"reviewed_profile\":"
                       << (PartyQuestSkyrimRuntimeCompatibilityEvidence::
                                   HasReviewedProfile(acExpectedQuestId)
                               ? "true"
                               : "false");
            }
            else
            {
                fields << ",\"available\":false"
                       << ",\"reason\":\"live-observation-failed-closed\"";
            }

            fields << ",\"grants_planning_authority\":false"
                   << ",\"grants_mutation_authority\":false";
            WriteEvent("runtime_compatibility_observed", fields.str());
        });
}

void PartyQuestP0LiveDiagnostics::RecordQuestObservation(
    TESQuest* apQuest,
    const QuestSnapshot& acSnapshot,
    const ModSystem& acModSystem,
    const char* acReason) noexcept
{
    if (!apQuest || !IsEnabled())
        return;

    RunDiagnostic(
        [&]()
        {
            const PartyQuestSyncFacts facts =
                QuestSnapshotCollector::CollectSyncFacts(apQuest);
            const PartyQuestSyncClassification classification =
                ClassifyPartyQuestSync(facts);
            const PartyQuestAdmissionDecision admission =
                PartyQuestAdmissionPolicy::Evaluate(acSnapshot.QuestId, facts);
            const PartyQuestApplyPlan applyPlan =
                PartyQuestRuntimeSafetyPolicy::BuildApplyPlan(admission, acSnapshot);

            const uint32_t localFormId = acModSystem.GetGameId(acSnapshot.QuestId);
            GameId roundTrip{};
            const bool roundTripMapped =
                localFormId != 0 &&
                acModSystem.GetServerModId(localFormId, roundTrip);
            const bool roundTripMatches =
                roundTripMapped && roundTrip == acSnapshot.QuestId;
            std::ostringstream stages;
            stages << '[';
            bool firstStage = true;
            for (TESQuest::Stage* pStage : apQuest->stages)
            {
                if (!pStage)
                    continue;
                if (!firstStage)
                    stages << ',';
                firstStage = false;
                stages << pStage->stageIndex;
            }
            stages << ']';

            std::ostringstream fields;
            fields << "\"reason\":\"" << EscapeJson(acReason) << "\""
                   << ",\"generation\":"
                   << PartyQuestRuntimeGenerationFence::GetProcessFence().GetGeneration()
                   << ",\"quest_id\":" << GameIdJson(acSnapshot.QuestId)
                   << ",\"observed_local_form_id\":" << apQuest->formID
                   << ",\"mapped_local_form_id\":" << localFormId
                   << ",\"round_trip_mapped\":"
                   << (roundTripMapped ? "true" : "false")
                   << ",\"round_trip_matches\":"
                   << (roundTripMatches ? "true" : "false")
                   << ",\"editor_id\":\""
                   << EscapeJson(apQuest->idName.AsAscii()) << "\""
                   << ",\"status\":\"" << StatusName(acSnapshot.Status) << "\""
                   << ",\"current_stage\":" << acSnapshot.CurrentStage
                   << ",\"defined_stages\":" << stages.str()
                   << ",\"snapshot_digest\":" << acSnapshot.ComputeDigest()
                   << ",\"completed_stage_count\":"
                   << acSnapshot.CompletedStages.size()
                   << ",\"objective_count\":" << acSnapshot.Objectives.size()
                   << ",\"reference_alias_count\":"
                   << acSnapshot.ReferenceAliases.size()
                   << ",\"location_alias_count\":"
                   << acSnapshot.LocationAliases.size()
                   << ",\"created_reference_count\":"
                   << acSnapshot.CreatedReferences.size()
                   << ",\"scene_participant_present\":"
                   << (acSnapshot.SceneParticipantPlayerId ? "true" : "false")
                   << ",\"quest_type\":" << static_cast<uint32_t>(facts.QuestType)
                   << ",\"has_stages\":" << (facts.HasStages ? "true" : "false")
                   << ",\"displayed_in_hud\":"
                   << (facts.IsDisplayedInHud ? "true" : "false")
                   << ",\"has_display_name\":"
                   << (facts.HasDisplayName ? "true" : "false")
                   << ",\"sync_class\":\""
                   << SyncClassName(classification.Class) << "\""
                   << ",\"admission\":\"" << AdmissionName(admission.Status)
                   << "\""
                   << ",\"runtime_safety\":\""
                   << SafetyName(applyPlan.Safety.Status) << "\""
                   << ",\"runtime_safety_reason\":\""
                   << SafetyReasonName(applyPlan.Safety.Reason) << "\""
                   << ",\"apply_actions\":"
                   << static_cast<uint32_t>(applyPlan.Actions)
                   << ",\"dry_run_only\":"
                   << (applyPlan.DryRunOnly ? "true" : "false")
                   << ",\"compatibility_facts\":{\"available\":false,\"reason\":\"recorded-only-at-canonical-planning-boundary\"}"
                   << ",\"canonical_mutation_attempted\":false";

            WriteEvent("quest_observed", fields.str());
        });
}
