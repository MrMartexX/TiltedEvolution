#include <Windows.h>
#include <bcrypt.h>

#include <cstdint>
#include <cstring>
#include <mutex>

namespace
{
using UInt32 = uint32_t;
using UInt64 = uint64_t;
using PluginHandle = UInt32;

constexpr UInt32 kInvalidPluginHandle = 0xFFFFFFFFu;
constexpr UInt32 kRuntimeVersion1597 = 0x01050610u;
constexpr UInt32 kSerializationInterfaceId = 3u;
constexpr UInt32 kMessagingInterfaceId = 5u;
constexpr UInt32 kSerializationInterfaceVersion = 4u;
constexpr UInt32 kMessagingInterfaceVersion = 2u;
constexpr UInt32 kSnapshotAbiVersion = 1u;
constexpr UInt32 kRecordVersion = 1u;

constexpr UInt32 FourCC(char a, char b, char c, char d) noexcept
{
    return static_cast<UInt32>(static_cast<uint8_t>(a)) |
        (static_cast<UInt32>(static_cast<uint8_t>(b)) << 8u) |
        (static_cast<UInt32>(static_cast<uint8_t>(c)) << 16u) |
        (static_cast<UInt32>(static_cast<uint8_t>(d)) << 24u);
}

constexpr UInt32 kSerializationUid = FourCC('P', 'Q', 'L', 'B');
constexpr UInt32 kRecordType = FourCC('L', 'N', 'G', '1');

struct PluginInfo
{
    static constexpr UInt32 kInfoVersion = 1u;

    UInt32 infoVersion;
    const char* name;
    UInt32 version;
};

struct SKSEInterface
{
    UInt32 skseVersion;
    UInt32 runtimeVersion;
    UInt32 editorVersion;
    UInt32 isEditor;
    void* (*QueryInterface)(UInt32 id);
    PluginHandle (*GetPluginHandle)();
    UInt32 (*GetReleaseIndex)();
    const PluginInfo* (*GetPluginInfo)(const char* name);
};

struct SKSESerializationInterface
{
    using EventCallback = void (*)(SKSESerializationInterface* intfc);
    using FormDeleteCallback = void (*)(UInt64 handle);

    UInt32 version;
    void (*SetUniqueID)(PluginHandle plugin, UInt32 uid);
    void (*SetRevertCallback)(PluginHandle plugin, EventCallback callback);
    void (*SetSaveCallback)(PluginHandle plugin, EventCallback callback);
    void (*SetLoadCallback)(PluginHandle plugin, EventCallback callback);
    void (*SetFormDeleteCallback)(PluginHandle plugin, FormDeleteCallback callback);
    bool (*WriteRecord)(UInt32 type, UInt32 version, const void* buffer, UInt32 length);
    bool (*OpenRecord)(UInt32 type, UInt32 version);
    bool (*WriteRecordData)(const void* buffer, UInt32 length);
    bool (*GetNextRecordInfo)(UInt32* type, UInt32* version, UInt32* length);
    UInt32 (*ReadRecordData)(void* buffer, UInt32 length);
    bool (*ResolveHandle)(UInt64 handle, UInt64* handleOut);
    bool (*ResolveFormId)(UInt32 formId, UInt32* formIdOut);
};

struct SKSEMessagingInterface
{
    struct Message
    {
        const char* sender;
        UInt32 type;
        UInt32 dataLen;
        void* data;
    };

    using EventCallback = void (*)(Message* message);

    enum : UInt32
    {
        kMessagePostLoad = 0u,
        kMessagePostPostLoad,
        kMessagePreLoadGame,
        kMessagePostLoadGame,
        kMessageSaveGame,
        kMessageDeleteGame,
        kMessageInputLoaded,
        kMessageNewGame,
        kMessageDataLoaded
    };

    UInt32 interfaceVersion;
    bool (*RegisterListener)(PluginHandle listener, const char* sender, EventCallback handler);
    bool (*Dispatch)(PluginHandle sender, UInt32 messageType, void* data, UInt32 dataLen, const char* receiver);
    void* (*GetEventDispatcher)(UInt32 dispatcherId);
};

enum class EvidenceState : UInt32
{
    Unavailable = 0u,
    CandidateUnpersisted = 1u,
    Persisted = 2u,
    Invalid = 3u
};

struct LineageId
{
    UInt64 High{};
    UInt64 Low{};

    [[nodiscard]] bool IsValid() const noexcept
    {
        return High != 0u || Low != 0u;
    }
};

struct LineageRecordV1
{
    UInt64 High{};
    UInt64 Low{};
    UInt64 Integrity{};
};

struct PartyQuestLineageBridgeSnapshot
{
    UInt32 AbiVersion{};
    UInt32 StructSize{};
    UInt64 Sequence{};
    UInt32 State{};
    UInt32 Reserved{};
    UInt64 ProfileHigh{};
    UInt64 ProfileLow{};
};

static_assert(sizeof(LineageRecordV1) == 24u);
static_assert(sizeof(PartyQuestLineageBridgeSnapshot) == 40u);

std::mutex g_stateMutex;
EvidenceState g_state = EvidenceState::Unavailable;
LineageId g_lineage{};
UInt64 g_sequence = 1u;

void AdvanceSequenceLocked() noexcept
{
    ++g_sequence;
    if (g_sequence == 0u)
        ++g_sequence;
}

void Publish(EvidenceState aState, LineageId aLineage = {}) noexcept
{
    std::lock_guard lock(g_stateMutex);
    g_state = aState;
    g_lineage = aLineage;
    AdvanceSequenceLocked();
}

UInt64 ComputeIntegrity(const LineageId& acLineage) noexcept
{
    constexpr UInt64 kFnvOffset = 14695981039346656037ull;
    constexpr UInt64 kFnvPrime = 1099511628211ull;
    constexpr UInt64 kDomain = 0x3156474E4C515450ull; // "PTQLNGV1" domain marker.

    UInt64 value = kFnvOffset;
    const UInt64 words[] = {kDomain, acLineage.High, acLineage.Low};
    for (const UInt64 word : words)
    {
        for (UInt32 shift = 0u; shift < 64u; shift += 8u)
        {
            value ^= static_cast<uint8_t>(word >> shift);
            value *= kFnvPrime;
        }
    }
    return value;
}

bool IsValidRecord(const LineageRecordV1& acRecord) noexcept
{
    const LineageId lineage{acRecord.High, acRecord.Low};
    return lineage.IsValid() && acRecord.Integrity == ComputeIntegrity(lineage);
}

bool GenerateLineage(LineageId& aLineage) noexcept
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        LineageId candidate{};
        const NTSTATUS status = BCryptGenRandom(
            nullptr,
            reinterpret_cast<PUCHAR>(&candidate),
            static_cast<ULONG>(sizeof(candidate)),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (status >= 0 && candidate.IsValid())
        {
            aLineage = candidate;
            return true;
        }
    }

    aLineage = {};
    return false;
}

void PublishNewGameCandidate() noexcept
{
    LineageId candidate{};
    if (!GenerateLineage(candidate))
    {
        Publish(EvidenceState::Invalid);
        return;
    }

    Publish(EvidenceState::CandidateUnpersisted, candidate);
}

void OnSerializationRevert(SKSESerializationInterface*)
{
    Publish(EvidenceState::Unavailable);
}

void OnSerializationSave(SKSESerializationInterface* apSerialization)
{
    if (!apSerialization || !apSerialization->WriteRecord)
        return;

    EvidenceState state{};
    LineageId lineage{};
    {
        std::lock_guard lock(g_stateMutex);
        state = g_state;
        lineage = g_lineage;
    }

    if ((state != EvidenceState::CandidateUnpersisted && state != EvidenceState::Persisted) ||
        !lineage.IsValid())
    {
        return;
    }

    const LineageRecordV1 record{
        lineage.High,
        lineage.Low,
        ComputeIntegrity(lineage)};

    // WriteRecord success only proves that SKSE accepted this bounded record for
    // the in-progress co-save. It is deliberately NOT promoted to Persisted here:
    // SKSE's save message/callback occurs before the complete Skyrim save pipeline
    // is known to have succeeded. Persisted evidence is issued only after a later
    // load reads the exact record back from that character's co-save.
    apSerialization->WriteRecord(
        kRecordType,
        kRecordVersion,
        &record,
        static_cast<UInt32>(sizeof(record)));
}

void OnSerializationLoad(SKSESerializationInterface* apSerialization)
{
    if (!apSerialization || !apSerialization->GetNextRecordInfo || !apSerialization->ReadRecordData)
    {
        Publish(EvidenceState::Invalid);
        return;
    }

    bool found = false;
    bool invalid = false;
    LineageRecordV1 loaded{};

    UInt32 type = 0u;
    UInt32 version = 0u;
    UInt32 length = 0u;
    while (apSerialization->GetNextRecordInfo(&type, &version, &length))
    {
        if (found || type != kRecordType || version != kRecordVersion || length != sizeof(LineageRecordV1))
        {
            invalid = true;
            break;
        }

        LineageRecordV1 candidate{};
        if (apSerialization->ReadRecordData(&candidate, static_cast<UInt32>(sizeof(candidate))) != sizeof(candidate))
        {
            invalid = true;
            break;
        }

        loaded = candidate;
        found = true;
    }

    if (invalid)
    {
        Publish(EvidenceState::Invalid);
        return;
    }

    if (!found)
    {
        Publish(EvidenceState::Unavailable);
        return;
    }

    if (!IsValidRecord(loaded))
    {
        Publish(EvidenceState::Invalid);
        return;
    }

    Publish(EvidenceState::Persisted, {loaded.High, loaded.Low});
}

void OnSkseMessage(SKSEMessagingInterface::Message* apMessage)
{
    if (!apMessage)
        return;

    switch (apMessage->type)
    {
    case SKSEMessagingInterface::kMessagePreLoadGame:
        // Invalidate old evidence before SKSE begins reading another save.
        Publish(EvidenceState::Unavailable);
        break;
    case SKSEMessagingInterface::kMessagePostLoadGame:
        // SKSE 2.0.20 dispatches the load result as a null/non-null pointer value.
        if (apMessage->data == nullptr)
            Publish(EvidenceState::Unavailable);
        break;
    case SKSEMessagingInterface::kMessageNewGame:
        // A new lineage exists in memory, but it is not authorization until the
        // record has survived a save and a later exact co-save load.
        PublishNewGameCandidate();
        break;
    default:
        break;
    }
}
} // namespace

extern "C" __declspec(dllexport) bool PartyQuestLineageBridge_GetSnapshot(
    PartyQuestLineageBridgeSnapshot* apSnapshot,
    uint32_t aSnapshotSize)
{
    if (!apSnapshot || aSnapshotSize != sizeof(PartyQuestLineageBridgeSnapshot))
        return false;

    std::lock_guard lock(g_stateMutex);
    PartyQuestLineageBridgeSnapshot snapshot{};
    snapshot.AbiVersion = kSnapshotAbiVersion;
    snapshot.StructSize = static_cast<UInt32>(sizeof(snapshot));
    snapshot.Sequence = g_sequence;
    snapshot.State = static_cast<UInt32>(g_state);
    snapshot.ProfileHigh = g_lineage.High;
    snapshot.ProfileLow = g_lineage.Low;
    std::memcpy(apSnapshot, &snapshot, sizeof(snapshot));
    return true;
}

extern "C" __declspec(dllexport) bool SKSEPlugin_Query(
    const SKSEInterface* apSkse,
    PluginInfo* apInfo)
{
    if (apInfo)
    {
        apInfo->infoVersion = PluginInfo::kInfoVersion;
        apInfo->name = "SkyrimTogetherLineageBridge";
        apInfo->version = 1u;
    }

    if (!apSkse || !apInfo)
        return false;

    return apSkse->isEditor == 0u &&
        apSkse->runtimeVersion == kRuntimeVersion1597 &&
        apSkse->QueryInterface != nullptr &&
        apSkse->GetPluginHandle != nullptr;
}

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSEInterface* apSkse)
{
    if (!apSkse || !apSkse->QueryInterface || !apSkse->GetPluginHandle)
        return false;

    const PluginHandle pluginHandle = apSkse->GetPluginHandle();
    if (pluginHandle == 0u || pluginHandle == kInvalidPluginHandle)
        return false;

    auto* pSerialization = static_cast<SKSESerializationInterface*>(
        apSkse->QueryInterface(kSerializationInterfaceId));
    auto* pMessaging = static_cast<SKSEMessagingInterface*>(
        apSkse->QueryInterface(kMessagingInterfaceId));

    if (!pSerialization || pSerialization->version < kSerializationInterfaceVersion ||
        !pSerialization->SetUniqueID || !pSerialization->SetRevertCallback ||
        !pSerialization->SetSaveCallback || !pSerialization->SetLoadCallback ||
        !pSerialization->WriteRecord || !pSerialization->GetNextRecordInfo ||
        !pSerialization->ReadRecordData ||
        !pMessaging || pMessaging->interfaceVersion < kMessagingInterfaceVersion ||
        !pMessaging->RegisterListener)
    {
        return false;
    }

    // Register the only fallible callback subscription before publishing any
    // serialization callbacks. Returning false after serialization registration
    // would let SKSE unload this DLL while retaining callback pointers into it.
    if (!pMessaging->RegisterListener(pluginHandle, "SKSE", &OnSkseMessage))
        return false;

    pSerialization->SetUniqueID(pluginHandle, kSerializationUid);
    pSerialization->SetRevertCallback(pluginHandle, &OnSerializationRevert);
    pSerialization->SetSaveCallback(pluginHandle, &OnSerializationSave);
    pSerialization->SetLoadCallback(pluginHandle, &OnSerializationLoad);

    Publish(EvidenceState::Unavailable);
    return true;
}
