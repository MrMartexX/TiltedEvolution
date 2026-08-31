#include <TiltedOnlinePCH.h>

#include <PartyQuestSkyrimNativeHookValidation.h>
#include <SaveLoad.h>
#include <Structs/Skyrim/PartyQuestRuntimeLifecycleIntegration.h>
#include <VersionDb.h>

#include <MinHook.h>

#include <array>
#include <cstdint>
#include <cstring>

struct Main;

bool __fastcall PartyQuest_BGSSaveLoadManager_SaveImpl(
    BGSSaveLoadManager*,
    int32_t,
    uint32_t,
    const char*);
bool __fastcall PartyQuest_BGSSaveLoadManager_LoadImpl(
    BGSSaveLoadManager*,
    const char*,
    int32_t,
    uint32_t,
    bool);
void PartyQuest_StartNewGame_Hook();
short __fastcall HookMainLoop(Main*);

namespace
{
struct RequiredHook final
{
    uint32_t AddressLibraryId;
    const char* Name;
    const void* Detour;
};

const std::array<RequiredHook, 4> kRequiredHooks{{
    {35727u, "BGSSaveLoadManager::Save_Impl", reinterpret_cast<const void*>(&PartyQuest_BGSSaveLoadManager_SaveImpl)},
    {35728u, "BGSSaveLoadManager::Load_Impl", reinterpret_cast<const void*>(&PartyQuest_BGSSaveLoadManager_LoadImpl)},
    {52118u, "StartNewGame", reinterpret_cast<const void*>(&PartyQuest_StartNewGame_Hook)},
    {36564u, "MainLoop", reinterpret_cast<const void*>(&HookMainLoop)},
}};

bool IsCommittedReadableAddress(const void* apAddress, size_t aSize) noexcept
{
    if (!apAddress || aSize == 0u)
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (::VirtualQuery(apAddress, &memory, sizeof(memory)) == 0 ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & PAGE_GUARD) != 0 ||
        (memory.Protect & PAGE_NOACCESS) != 0)
    {
        return false;
    }

    const auto address = reinterpret_cast<uintptr_t>(apAddress);
    const auto regionBase = reinterpret_cast<uintptr_t>(memory.BaseAddress);
    const auto regionEnd = regionBase + memory.RegionSize;
    return address >= regionBase &&
        aSize <= regionEnd - address;
}

bool IsExecutableProtection(DWORD aProtection) noexcept
{
    switch (aProtection & 0xFFu)
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
    return ::VirtualQuery(apAddress, &memory, sizeof(memory)) != 0 &&
        memory.State == MEM_COMMIT &&
        (memory.Protect & PAGE_GUARD) == 0 &&
        (memory.Protect & PAGE_NOACCESS) == 0 &&
        IsExecutableProtection(memory.Protect);
}

const uint8_t* FollowRelativeJump(const uint8_t* apPatch) noexcept
{
    if (!IsCommittedReadableAddress(apPatch, 5u) || apPatch[0] != 0xE9u)
        return nullptr;

    int32_t displacement{};
    std::memcpy(&displacement, apPatch + 1u, sizeof(displacement));
    return apPatch + 5u + displacement;
}

const void* ResolveMinHookDetour(const void* apTarget) noexcept
{
#if !defined(_M_AMD64)
    (void)apTarget;
    return nullptr;
#else
    auto* target = reinterpret_cast<const uint8_t*>(apTarget);
    if (!IsCommittedReadableAddress(target, 5u))
        return nullptr;

    // MinHook normally writes E9 rel32 at the function entry. For very short
    // prologues it uses the documented hot-patch form: E9 at target-5 followed
    // by EB F9 at target. Accept only those two MinHook-owned layouts.
    const uint8_t* relay = nullptr;
    if (target[0] == 0xE9u)
    {
        relay = FollowRelativeJump(target);
    }
    else if (target[0] == 0xEBu &&
             IsCommittedReadableAddress(target, 2u) &&
             static_cast<int8_t>(target[1]) == -7)
    {
        relay = FollowRelativeJump(target - 5u);
    }

    if (!relay || !IsCommittedReadableAddress(relay, 14u))
        return nullptr;

    MEMORY_BASIC_INFORMATION relayMemory{};
    if (::VirtualQuery(relay, &relayMemory, sizeof(relayMemory)) == 0 ||
        relayMemory.State != MEM_COMMIT ||
        !IsExecutableProtection(relayMemory.Protect))
    {
        return nullptr;
    }

    // MinHook x64 relay is FF 25 00000000 followed by the absolute detour
    // address. This is MinHook's own trampoline contract, not a Skyrim prologue
    // signature, so it remains stable across supported Skyrim binaries.
    if (relay[0] != 0xFFu || relay[1] != 0x25u)
        return nullptr;

    int32_t slotDisplacement{};
    std::memcpy(&slotDisplacement, relay + 2u, sizeof(slotDisplacement));
    const auto* slot = relay + 6u + slotDisplacement;
    if (!IsCommittedReadableAddress(slot, sizeof(uintptr_t)))
        return nullptr;

    uintptr_t detour{};
    std::memcpy(&detour, slot, sizeof(detour));
    return reinterpret_cast<const void*>(detour);
#endif
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

    // TiltedReverse's delayed hook manager currently discards the return values
    // from MH_CreateHook and MH_EnableHook. Re-enable the target first so a hook
    // that was successfully created but transiently failed to enable gets one
    // deterministic recovery attempt. A hook created by somebody else is not
    // accepted merely because this call reports it enabled: the relay is then
    // decoded below and must point to our exact detour.
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

    const void* const installedDetour = ResolveMinHookDetour(target);
    if (installedDetour != acHook.Detour)
    {
        spdlog::critical(
            "PartyQuest required native hook target is not routed to the expected detour: name={} addressLibraryId={} target={} expectedDetour={} installedDetour={}",
            acHook.Name,
            acHook.AddressLibraryId,
            target,
            acHook.Detour,
            installedDetour);
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

    // Installer callbacks have already recorded the exact required lifecycle
    // targets that resolved and were queued. Only now, after proving the live
    // MinHook patches route to our exact detours, may that evidence become
    // production-visible lifecycle coverage.
    PartyQuestRuntimeLifecycleIntegrationPolicy::
        ConfirmNativeHookCommitValidated();

    if (!PartyQuestRuntimeLifecycleIntegrationPolicy::
            HasCompleteCharacterIdentityCoverage())
    {
        spdlog::critical(
            "PartyQuest native hooks validated but lifecycle coverage ledger is incomplete; refusing startup");
        return false;
    }

    spdlog::info(
        "PartyQuest validated all required pre-entry native hooks after MinHook commit");
    return true;
}
