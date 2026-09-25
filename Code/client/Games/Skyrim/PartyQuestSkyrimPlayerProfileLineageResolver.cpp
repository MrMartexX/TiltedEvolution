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

class ScopedModuleReference final
{
public:
    explicit ScopedModuleReference(HMODULE aModule) noexcept
        : m_module(aModule)
    {
    }

    ~ScopedModuleReference() noexcept
    {
        if (m_module)
            ::FreeLibrary(m_module);
    }

    ScopedModuleReference(const ScopedModuleReference&) = delete;
    ScopedModuleReference& operator=(const ScopedModuleReference&) = delete;

private:
    HMODULE m_module{};
};

[[nodiscard]] bool SameSnapshot(
    const PartyQuestLineageBridgeSnapshot& acFirst,
    const PartyQuestLineageBridgeSnapshot& acSecond) noexcept
{
    return acSecond.AbiVersion == acFirst.AbiVersion &&
        acSecond.StructSize == acFirst.StructSize &&
        acSecond.Sequence == acFirst.Sequence &&
        acSecond.State == acFirst.State &&
        acSecond.Reserved == acFirst.Reserved &&
        acSecond.ProfileHigh == acFirst.ProfileHigh &&
        acSecond.ProfileLow == acFirst.ProfileLow;
}

[[nodiscard]] PartyQuestLineageRuntimeVersion DescribeRuntime(
    const PartyQuestLineageProviderDescriptor& acProvider) noexcept
{
    return {
        acProvider.RuntimeMajor,
        acProvider.RuntimeMinor,
        acProvider.RuntimePatch,
        acProvider.RuntimeBuild};
}
} // namespace

PartyQuestPlayerProfileLineageAuthorization
PartyQuestSkyrimPlayerProfileLineageResolver::Resolve() noexcept
{
    return ResolveDetailed().Authorization;
}

PartyQuestPlayerProfileLineageResolveResult
PartyQuestSkyrimPlayerProfileLineageResolver::ResolveDetailed() noexcept
{
    PartyQuestPlayerProfileLineageResolveResult result;

    // The SKSE loader is the trust boundary for this bridge. Acquire a reference
    // only to the already-loaded module; never LoadLibrary an arbitrary DLL from
    // the search path merely to manufacture evidence. The reference pins the
    // export code for the complete ABI call sequence.
    HMODULE module = nullptr;
    if (!::GetModuleHandleExW(0u, kBridgeModule, &module) || !module)
    {
        result.Status =
            PartyQuestPlayerProfileLineageResolveStatus::BridgeUnavailable;
        return result;
    }
    const ScopedModuleReference moduleReference(module);

    const auto getSnapshot = reinterpret_cast<TGetLineageBridgeSnapshot*>(
        ::GetProcAddress(module, kBridgeExport));
    const auto getDescriptor = reinterpret_cast<TGetLineageProviderDescriptor*>(
        ::GetProcAddress(module, kProviderDescriptorExport));
    if (!getSnapshot || !getDescriptor)
    {
        result.Status =
            PartyQuestPlayerProfileLineageResolveStatus::RequiredExportMissing;
        return result;
    }

    const auto& versionDb = VersionDb::Get();
    if (!versionDb.IsLoaded())
    {
        result.Status = PartyQuestPlayerProfileLineageResolveStatus::
            RuntimeDatabaseUnavailable;
        return result;
    }

    int major = 0;
    int minor = 0;
    int patch = 0;
    int build = 0;
    versionDb.GetLoadedVersion(major, minor, patch, build);
    if (major <= 0 || minor < 0 || patch < 0 || build < 0)
    {
        result.Status = PartyQuestPlayerProfileLineageResolveStatus::
            RuntimeDatabaseUnavailable;
        return result;
    }

    const PartyQuestLineageRuntimeVersion expectedRuntime{
        static_cast<uint32_t>(major),
        static_cast<uint32_t>(minor),
        static_cast<uint32_t>(patch),
        static_cast<uint32_t>(build)};
    if (!PartyQuestLineageTargetRuntimeRegistry::IsTarget(expectedRuntime))
    {
        result.Status =
            PartyQuestPlayerProfileLineageResolveStatus::UnsupportedRuntime;
        return result;
    }

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const uint64_t generation = fence.GetGeneration();
    auto generationLease = fence.TryAcquire(generation);
    if (!generationLease || !generationLease->IsValid())
    {
        result.Status =
            PartyQuestPlayerProfileLineageResolveStatus::GenerationUnavailable;
        return result;
    }

    PartyQuestLineageProviderDescriptor provider{};
    if (!getDescriptor(&provider, static_cast<uint32_t>(sizeof(provider))))
    {
        result.Status =
            PartyQuestPlayerProfileLineageResolveStatus::ProviderReadFailed;
        return result;
    }

    if (provider.AbiVersion != kPartyQuestLineageProviderAbiVersion ||
        provider.StructSize !=
            static_cast<uint32_t>(sizeof(PartyQuestLineageProviderDescriptor)))
    {
        result.Status =
            PartyQuestPlayerProfileLineageResolveStatus::UnsupportedProviderAbi;
        return result;
    }

    if (DescribeRuntime(provider) != expectedRuntime)
    {
        result.Status =
            PartyQuestPlayerProfileLineageResolveStatus::UnsupportedRuntime;
        return result;
    }

    if (!PartyQuestLineageTargetRuntimeRegistry::IsApprovedDescriptor(
            provider,
            expectedRuntime))
    {
        result.Status =
            PartyQuestPlayerProfileLineageResolveStatus::UnsupportedProvider;
        return result;
    }

    PartyQuestLineageBridgeSnapshot first{};
    PartyQuestLineageBridgeSnapshot second{};
    if (!getSnapshot(&first, static_cast<uint32_t>(sizeof(first))) ||
        !getSnapshot(&second, static_cast<uint32_t>(sizeof(second))))
    {
        result.Status =
            PartyQuestPlayerProfileLineageResolveStatus::SnapshotReadFailed;
        return result;
    }

    if (!SameSnapshot(first, second))
    {
        result.Status =
            PartyQuestPlayerProfileLineageResolveStatus::UnstableSnapshot;
        return result;
    }

    result.Authorization = ResolveStableSnapshots(
        provider,
        expectedRuntime,
        first,
        second,
        generationLease->GetGeneration());
    if (!result.Authorization.IsVerified())
    {
        result.Status =
            PartyQuestPlayerProfileLineageResolveStatus::InvalidSnapshot;
        return result;
    }

    result.Status = PartyQuestPlayerProfileLineageResolveStatus::Verified;
    return result;
}
