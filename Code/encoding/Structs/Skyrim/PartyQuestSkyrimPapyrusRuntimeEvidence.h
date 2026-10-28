#pragma once

#include <Structs/Skyrim/PartyQuestSkyrimPapyrusRuntimeProfileResolver.h>

#include <cstdint>

enum class PartyQuestSkyrimPapyrusRuntimeEvidenceStatus : uint8_t
{
    Supported,
    WrongRuntime,
    WrongExecutable,
    AddressLibraryUnavailable,
    WrongAddressLibraryFormat,
    WrongVirtualTableRecord,
    InvalidVirtualTable
};

struct PartyQuestSkyrimPapyrusRuntimeEvidence final
{
    PartyQuestSkyrimRuntimeVersion RuntimeVersion{};
    PartyQuestSkyrimExecutableIdentity ExecutableIdentity{};
    bool AddressLibraryLoaded{};
    uint32_t AddressLibraryFormat{};
    uint32_t VirtualTableId{};
    uint64_t VirtualTableOffset{};
    bool VirtualTableReadable{};
    bool RequiredEntriesExecutable{};

    [[nodiscard]] PartyQuestSkyrimPapyrusRuntimeEvidenceStatus Validate() const noexcept
    {
        constexpr PartyQuestSkyrimRuntimeVersion runtime{1, 6, 1170, 0};
        constexpr PartyQuestSkyrimExecutableIdentity executable{{
            0xC4, 0x34, 0x20, 0x88, 0x94, 0xF0, 0x7F, 0x60,
            0x4B, 0x85, 0x2F, 0x29, 0xB8, 0xED, 0xC3, 0xA5,
            0x8C, 0x4D, 0xE6, 0x3D, 0xE7, 0x83, 0x37, 0x37,
            0x33, 0xE7, 0x2B, 0x2B, 0x73, 0xF3, 0x3B, 0xE9}};

        if (!RuntimeVersion.Matches(runtime))
            return PartyQuestSkyrimPapyrusRuntimeEvidenceStatus::WrongRuntime;
        if (!ExecutableIdentity.Matches(executable))
            return PartyQuestSkyrimPapyrusRuntimeEvidenceStatus::WrongExecutable;
        if (!AddressLibraryLoaded)
            return PartyQuestSkyrimPapyrusRuntimeEvidenceStatus::AddressLibraryUnavailable;
        if (AddressLibraryFormat != 2u)
            return PartyQuestSkyrimPapyrusRuntimeEvidenceStatus::WrongAddressLibraryFormat;
        if (VirtualTableId != 252631u || VirtualTableOffset != 0x1AA0B48ull)
            return PartyQuestSkyrimPapyrusRuntimeEvidenceStatus::WrongVirtualTableRecord;
        if (!VirtualTableReadable || !RequiredEntriesExecutable)
            return PartyQuestSkyrimPapyrusRuntimeEvidenceStatus::InvalidVirtualTable;
        return PartyQuestSkyrimPapyrusRuntimeEvidenceStatus::Supported;
    }
};
