#include <TiltedOnlinePCH.h>

#include <Structs/Skyrim/PartyQuestPlayerProfileLineage.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>

namespace
{
using TGetLineageBridgeSnapshot = bool(
    PartyQuestLineageBridgeSnapshot*,
    uint32_t);

constexpr wchar_t kBridgeModule[] = L"SkyrimTogetherLineageBridge.dll";
constexpr char kBridgeExport[] = "PartyQuestLineageBridge_GetSnapshot";
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
    if (!getSnapshot)
        return {};

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const uint64_t generation = fence.GetGeneration();
    auto generationLease = fence.TryAcquire(generation);
    if (!generationLease || !generationLease->IsValid())
        return {};

    PartyQuestLineageBridgeSnapshot first{};
    PartyQuestLineageBridgeSnapshot second{};
    if (!getSnapshot(&first, static_cast<uint32_t>(sizeof(first))) ||
        !getSnapshot(&second, static_cast<uint32_t>(sizeof(second))))
    {
        return {};
    }

    return ResolveStableSnapshots(first, second, generation);
}
