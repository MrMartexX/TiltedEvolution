#include <Structs/Skyrim/QuestSnapshot.h>

#include <algorithm>
#include <cstddef>
#include <type_traits>

namespace
{
constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

template <class T, bool = std::is_enum_v<T>>
struct UnderlyingOrSelf
{
    using Type = T;
};

template <class T>
struct UnderlyingOrSelf<T, true>
{
    using Type = std::underlying_type_t<T>;
};

class StableDigest final
{
public:
    template <class T>
        requires(std::is_integral_v<T> || std::is_enum_v<T>)
    void Append(T aValue) noexcept
    {
        using ValueType = typename UnderlyingOrSelf<T>::Type;

        if constexpr (std::is_same_v<ValueType, bool>)
        {
            AppendByte(aValue ? 1 : 0);
        }
        else
        {
            using UnsignedType = std::make_unsigned_t<ValueType>;
            const auto value = static_cast<UnsignedType>(aValue);
            for (size_t i = 0; i < sizeof(UnsignedType); ++i)
                AppendByte(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
        }
    }

    void Append(const GameId& acId) noexcept
    {
        Append(acId.ModId);
        Append(acId.BaseId);
    }

    template <class T, class Callback>
    void AppendVector(const std::vector<T>& acValues, Callback&& aCallback) noexcept
    {
        Append(static_cast<uint32_t>(acValues.size()));
        for (const auto& value : acValues)
            aCallback(*this, value);
    }

    [[nodiscard]] uint64_t Get() const noexcept { return m_value; }

private:
    void AppendByte(uint8_t aValue) noexcept
    {
        m_value ^= aValue;
        m_value *= kFnvPrime;
    }

    uint64_t m_value{kFnvOffsetBasis};
};

bool GameIdLess(const GameId& acLeft, const GameId& acRight) noexcept
{
    if (acLeft.ModId != acRight.ModId)
        return acLeft.ModId < acRight.ModId;

    return acLeft.BaseId < acRight.BaseId;
}

bool OptionalGameIdLess(const std::optional<GameId>& acLeft, const std::optional<GameId>& acRight) noexcept
{
    if (acLeft.has_value() != acRight.has_value())
        return !acLeft.has_value();

    if (!acLeft)
        return false;

    return GameIdLess(*acLeft, *acRight);
}

template <class T>
void SortAndUnique(std::vector<T>& aValues)
{
    std::sort(aValues.begin(), aValues.end());
    aValues.erase(std::unique(aValues.begin(), aValues.end()), aValues.end());
}
} // namespace

void QuestSnapshot::Canonicalize()
{
    SortAndUnique(CompletedStages);

    std::sort(Objectives.begin(), Objectives.end(), [](const auto& acLeft, const auto& acRight)
    {
        if (acLeft.Index != acRight.Index)
            return acLeft.Index < acRight.Index;

        return acLeft.State < acRight.State;
    });
    Objectives.erase(std::unique(Objectives.begin(), Objectives.end()), Objectives.end());

    std::sort(ReferenceAliases.begin(), ReferenceAliases.end(), [](const auto& acLeft, const auto& acRight)
    {
        if (acLeft.AliasId != acRight.AliasId)
            return acLeft.AliasId < acRight.AliasId;
        if (acLeft.ReferenceId != acRight.ReferenceId)
            return OptionalGameIdLess(acLeft.ReferenceId, acRight.ReferenceId);

        return acLeft.IsQuestObject < acRight.IsQuestObject;
    });
    ReferenceAliases.erase(std::unique(ReferenceAliases.begin(), ReferenceAliases.end()), ReferenceAliases.end());

    std::sort(LocationAliases.begin(), LocationAliases.end(), [](const auto& acLeft, const auto& acRight)
    {
        if (acLeft.AliasId != acRight.AliasId)
            return acLeft.AliasId < acRight.AliasId;

        return OptionalGameIdLess(acLeft.LocationId, acRight.LocationId);
    });
    LocationAliases.erase(std::unique(LocationAliases.begin(), LocationAliases.end()), LocationAliases.end());

    std::sort(CreatedReferences.begin(), CreatedReferences.end(), GameIdLess);
    CreatedReferences.erase(std::unique(CreatedReferences.begin(), CreatedReferences.end()), CreatedReferences.end());
}

uint64_t QuestSnapshot::ComputeDigest() const
{
    QuestSnapshot snapshot = *this;
    snapshot.Canonicalize();

    StableDigest digest;
    digest.Append(SchemaVersion);
    digest.Append(snapshot.QuestId);
    digest.Append(snapshot.Status);
    digest.Append(snapshot.CurrentStage);
    digest.Append(snapshot.Revision);
    digest.Append(snapshot.InitiatorPlayerId);

    digest.Append(snapshot.SceneParticipantPlayerId.has_value());
    if (snapshot.SceneParticipantPlayerId)
        digest.Append(*snapshot.SceneParticipantPlayerId);

    digest.AppendVector(snapshot.CompletedStages, [](StableDigest& aDigest, uint16_t aStage)
    {
        aDigest.Append(aStage);
    });

    digest.AppendVector(snapshot.Objectives, [](StableDigest& aDigest, const QuestObjectiveSnapshot& acObjective)
    {
        aDigest.Append(acObjective.Index);
        aDigest.Append(acObjective.State);
    });

    digest.AppendVector(snapshot.ReferenceAliases, [](StableDigest& aDigest, const QuestReferenceAliasSnapshot& acAlias)
    {
        aDigest.Append(acAlias.AliasId);
        aDigest.Append(acAlias.ReferenceId.has_value());
        if (acAlias.ReferenceId)
            aDigest.Append(*acAlias.ReferenceId);
        aDigest.Append(acAlias.IsQuestObject);
    });

    digest.AppendVector(snapshot.LocationAliases, [](StableDigest& aDigest, const QuestLocationAliasSnapshot& acAlias)
    {
        aDigest.Append(acAlias.AliasId);
        aDigest.Append(acAlias.LocationId.has_value());
        if (acAlias.LocationId)
            aDigest.Append(*acAlias.LocationId);
    });

    digest.AppendVector(snapshot.CreatedReferences, [](StableDigest& aDigest, const GameId& acReference)
    {
        aDigest.Append(acReference);
    });

    return digest.Get();
}
