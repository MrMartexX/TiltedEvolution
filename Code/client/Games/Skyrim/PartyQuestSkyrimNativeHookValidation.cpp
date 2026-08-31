#include <TiltedOnlinePCH.h>

#include <PartyQuestSkyrimNativeHookValidation.h>
#include <SaveLoad.h>
#include <Structs/Skyrim/PartyQuestRuntimeLifecycleIntegration.h>
#include <VersionDb.h>

#include <MinHook.h>

#include <array>
#include <cstdint>

namespace
{
struct RequiredHook final
{
    uint32_t AddressLibraryId;
    const char* Name;
};

constexpr std::array<RequiredHook, 4> kRequiredHooks{{
    {35727u, "BGSSaveLoadManager::Save_Impl"},
    {35728u, "BGSSaveLoadManager::Load_Impl"},
    {52118u, "StartNewGame"},
    {36564u, "MainLoop"},
}};

bool IsExecutableMainModuleAddress(const void* apAddress) noexcept
{
    if (!apAddress)
        return false;

    const auto module = reinterpret_cast<const uint8_t*>(::GetModuleHandleW(nullptr));
    if (!module)
        return false;

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0)
        return false;

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;

    const auto address = reinterpret_cast<uintptr_t>(apAddress);
    const auto base = reinterpret_cast<uintptr_t>(module);
    const auto imageSize = static_cast<uintptr_t>(nt->OptionalHeader.SizeOfImage);
    if (address < base || address - base >= imageSize)
        return false;

    const uint64_t rva = static_cast<uint64_t>(address - base);
    const auto* sections = IMAGE_FIRST_SECTION(nt);
    bool executableSection = false;
    for (uint16_t i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        const auto& section = sections[i];
        const uint64_t sectionStart = section.VirtualAddress;
        const uint64_t sectionSpan =
            section.Misc.VirtualSize > section.SizeOfRawData
                ? section.Misc.VirtualSize
                : section.SizeOfRawData;
        const uint64_t sectionEnd = sectionStart + sectionSpan;
        if (rva >= sectionStart && rva < sectionEnd)
        {
            executableSection =
                (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
            break;
        }
    }

    if (!executableSection)
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (::VirtualQuery(apAddress, &memory, sizeof(memory)) == 0 ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & PAGE_GUARD) != 0 ||
        (memory.Protect & PAGE_NOACCESS) != 0)
    {
        return false;
    }

    switch (memory.Protect & 0xFFu)
    {
    case PAGE_EXECUTE:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return true;
    default:
        return false;
    }
}

bool ValidateEnabledHook(const RequiredHook& acHook) noexcept
{
    void* const target = VersionDb::Get().FindAddressById(acHook.AddressLibraryId);
    if (!target)
    {
        spdlog::critical(
            "PartyQuest required native hook target did not resolve: name={} addressLibraryId={}",
            acHook.Name,
            acHook.AddressLibraryId);
        return false;
    }

    if (!IsExecutableMainModuleAddress(target))
    {
        spdlog::critical(
            "PartyQuest required native hook target is outside executable Skyrim image memory: name={} addressLibraryId={} target={}",
            acHook.Name,
            acHook.AddressLibraryId,
            target);
        return false;
    }

    // TiltedReverse's delayed hook manager currently does not surface
    // MH_CreateHook/MH_EnableHook failures. Re-issuing MH_EnableHook is a safe
    // post-commit proof: an already-enabled hook returns MH_ERROR_ENABLED, a
    // created-but-not-enabled hook can be enabled here with MH_OK, and a target
    // that was never created remains a hard failure (for example
    // MH_ERROR_NOT_CREATED). This turns the P0-required subset fail-closed
    // without guessing function prologues or weakening the shared hook layer.
    const MH_STATUS status = ::MH_EnableHook(target);
    if (status != MH_OK && status != MH_ERROR_ENABLED)
    {
        spdlog::critical(
            "PartyQuest required native hook is not enabled after commit: name={} addressLibraryId={} target={} minHookStatus={} message={}",
            acHook.Name,
            acHook.AddressLibraryId,
            target,
            static_cast<int>(status),
            ::MH_StatusToString(status));
        return false;
    }

    return true;
}
} // namespace

bool PartyQuestSkyrimNativeHookValidator::ValidateAndPublish() noexcept
{
    if (!VersionDb::Get().IsLoaded())
    {
        spdlog::critical(
            "PartyQuest cannot validate native hooks because Address Library is not loaded");
        return false;
    }

    for (const auto& hook : kRequiredHooks)
    {
        if (!ValidateEnabledHook(hook))
            return false;
    }

    // Publish installation evidence only after the complete required set has
    // actually survived MinHook creation/enabling. Merely resolving an Address
    // Library entry or queueing TP_HOOK is not installation evidence.
    ConfirmPartyQuestSaveHookInstalled();
    PartyQuestRuntimeLifecycleIntegrationPolicy::MarkVerifiedPreTransitionHook(
        PartyQuestRuntimeLifecycleEvent::LoadGame);
    PartyQuestRuntimeLifecycleIntegrationPolicy::MarkVerifiedPreTransitionHook(
        PartyQuestRuntimeLifecycleEvent::NewGame);
    PartyQuestRuntimeLifecycleIntegrationPolicy::MarkVerifiedPreTransitionHook(
        PartyQuestRuntimeLifecycleEvent::MainMenu);

    spdlog::info(
        "PartyQuest validated all required pre-entry native hooks after MinHook commit");
    return true;
}
