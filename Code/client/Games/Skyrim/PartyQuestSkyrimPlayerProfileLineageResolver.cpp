#include <TiltedOnlinePCH.h>

#include <Structs/Skyrim/PartyQuestPlayerProfileLineage.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <VersionDb.h>

namespace
{
using TGetLineageBridgeSnapshot = bool(
    PartyQuestLineageBridgeSnapshot*,
    uint32_t);
using TGetLineageProviderDescriptor = bool(
    PartyQuestLineageProviderDescriptor*,
    uint32_t);

constexpr wchar_t kBridgeModule[] = L"SkyrimTogetherLineageBridge.dll";
constexpr char kBridgeExport[] = "PartyQuestLineageBridge_GetSnapshot";
constexpr char kProviderDescriptorExport[] =
    "PartyQuestLineageProvider_GetDescriptor";
} // namespace

PartyQuestPlayerProfileLineageAuthorization
PartyQuestSkyrimPlayerProfileLineageResolver::Resolve() noexcept
{
    // The SKSE loader is the trust boundary for this bridge. Never LoadLibrary
    // an arbitrary DLL from the search path merely to manufacture evidence.
    const HMODULE module = ::GetModuleHandleW(kBridgeModule);
    if (!module)
        return {};

    const auto getSnapshot = reinterpret_cast<TGetLineageBridgeSnapshot*>(
        ::GetProcAddress(module, kBridgeExport));
    const auto getDescriptor = reinterpret_cast<TGetLineageProviderDescriptor*>(
        ::GetProcAddress(module, kProviderDescriptorExport));
    if (!getSnapshot || !getDescriptor)
        return {};

    const auto& versionDb = VersionDb::Get();
    if (!versionDb.IsLoaded())
        return {};

    int major = 0;
    int minor = 0;
    int patch = 0;
    int build = 0;
    versionDb.GetLoadedVersion(major, minor, patch, build);
    if (major <= 0 || minor < 0 || patch < 0 || build < 0)
        return {};

    const PartyQuestLineageRuntimeVersion expectedRuntime{
        static_cast<uint32_t>(major),
        static_cast<uint32_t>(minor),
        static_cast<uint32_t>(patch),
        static_cast<uint32_t>(build)};

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const uint64_t generation = fence.GetGeneration();
    auto generationLease = fence.TryAcquire(generation);
    if (!generationLease || !generationLease->IsValid())
        return {};

    PartyQuestLineageProviderDescriptor provider{};
    PartyQuestLineageBridgeSnapshot first{};
    PartyQuestLineageBridgeSnapshot second{};
    if (!getDescriptor(&provider, static_cast<uint32_t>(sizeof(provider))) ||
        !getSnapshot(&first, static_cast<uint32_t>(sizeof(first))) ||
        !getSnapshot(&second, static_cast<uint32_t>(sizeof(second))))
    {
        return {};
    }

    return ResolveStableSnapshots(
        provider,
        expectedRuntime,
        first,
        second,
        generation);
}
