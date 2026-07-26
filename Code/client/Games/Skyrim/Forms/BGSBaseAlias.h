#pragma once

#include <Misc/BSString.h>

struct TESForm;
struct TESQuest;

/**
 * Runtime layout shared by Skyrim reference and location quest aliases.
 *
 * Only fields required by the quest snapshot PoC are represented here. The
 * layout matches the 64-bit Skyrim SE runtime structure.
 */
struct BGSBaseAlias
{
    enum Flags : uint32_t
    {
        Reserves = 1u << 0,
        Optional = 1u << 1,
        QuestObject = 1u << 2,
        AllowReuse = 1u << 3,
        AllowDead = 1u << 4,
        LoadedOnly = 1u << 5,
        Essential = 1u << 6,
        AllowDisabled = 1u << 7,
        StoreName = 1u << 8,
        AllowReserved = 1u << 9,
        Protected = 1u << 10,
        ForcedFromAlias = 1u << 11,
        AllowDestroyed = 1u << 12,
        FindPlayerClosest = 1u << 13,
        UsesNames = 1u << 14,
        InitiallyDisabled = 1u << 15,
        AllowCleared = 1u << 16,
        ClearNameOnRemove = 1u << 17,
        ActorsOnly = 1u << 18,
        Transient = 1u << 19,
        ExternalLink = 1u << 20,
        NoPickpocket = 1u << 21,
        DataAlias = 1u << 22,
        SceneOptional = 1u << 24,
        CreateIn = 1u << 31
    };

    enum class FillType : uint16_t
    {
        Conditions = 0,
        Forced = 1,
        FromAlias = 2,
        FromEvent = 3,
        Created = 4,
        FromExternal = 5,
        UniqueActor = 6,
        NearAlias = 7
    };

    virtual ~BGSBaseAlias() = default;
    virtual bool Load(void* apMod) = 0;
    virtual void InitItem(TESForm* apForm) = 0;
    [[nodiscard]] virtual const BSFixedString& QType() const = 0;

    [[nodiscard]] bool IsQuestObject() const noexcept { return (flags & Flags::QuestObject) != 0; }

    BSFixedString aliasName; // 08
    TESQuest* owningQuest;  // 10
    uint32_t aliasId;       // 18
    uint32_t flags;         // 1C
    FillType fillType;      // 20
    uint16_t pad22;         // 22
    uint32_t pad24;         // 24
};

static_assert(sizeof(BGSBaseAlias) == 0x28);
static_assert(offsetof(BGSBaseAlias, aliasId) == 0x18);
static_assert(offsetof(BGSBaseAlias, flags) == 0x1C);
static_assert(offsetof(BGSBaseAlias, fillType) == 0x20);
