#include <TiltedCore/Stl.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <Messages/ClientMessageFactory.h>
#include <Messages/ServerMessageFactory.h>
#include <catch2/catch.hpp>

namespace
{
template <class T>
void PopulateIntakeEnvelope(T& aMessage)
{
    // Older user-provided message constructors leave these scalar fields
    // uninitialized. Supply valid fixtures instead of reading indeterminate data.
    if constexpr (requires { aMessage.Id = uint32_t{}; }) aMessage.Id = 29;
    if constexpr (requires { aMessage.ActorId = uint32_t{}; }) aMessage.ActorId = 31;
    if constexpr (requires { aMessage.PlayerId = uint32_t{}; }) aMessage.PlayerId = 41;
    if constexpr (requires { aMessage.ActivatorId = uint32_t{}; }) aMessage.ActivatorId = 17;
    if constexpr (requires { aMessage.PreActivationOpenState = uint8_t{}; }) aMessage.PreActivationOpenState = 0;
    if constexpr (requires { aMessage.CasterId = uint32_t{}; }) aMessage.CasterId = 19;
    if constexpr (requires { aMessage.CastingSource = int32_t{}; }) aMessage.CastingSource = 0;
    if constexpr (requires { aMessage.IsLocked = bool{}; }) aMessage.IsLocked = true;
    if constexpr (requires { aMessage.LockLevel = uint8_t{}; }) aMessage.LockLevel = 20;
    if constexpr (requires { aMessage.IsDead = bool{}; }) aMessage.IsDead = true;
    if constexpr (requires { aMessage.DeltaHealth = float{}; }) aMessage.DeltaHealth = -2.5f;
    if constexpr (requires { aMessage.IsLeader = bool{}; }) aMessage.IsLeader = true;
    if constexpr (requires { aMessage.LeaderPlayerId = uint32_t{}; }) aMessage.LeaderPlayerId = 37;
    if constexpr (requires { aMessage.Stage = uint16_t{}; }) aMessage.Stage = 20;
    if constexpr (requires { aMessage.Status = uint8_t{}; }) aMessage.Status = 1;
    if constexpr (requires { aMessage.ClientQuestType = uint8_t{}; }) aMessage.ClientQuestType = 4;
    if constexpr (requires { aMessage.FormID = uint32_t{}; }) aMessage.FormID = 0x14;
    if constexpr (requires { aMessage.IsDualCasting = bool{}; }) aMessage.IsDualCasting = true;
    if constexpr (requires { aMessage.DesiredTarget = uint32_t{}; }) aMessage.DesiredTarget = 23;
    // Distinct high-bit values detect truncation and swapped identity fields.
    if constexpr (requires { aMessage.ServerId = uint32_t{}; })
        aMessage.ServerId = 0xA147C368u;
    if constexpr (requires { aMessage.OwnershipEpoch = uint32_t{}; })
        aMessage.OwnershipEpoch = 0xFEDCBA98u;
    if constexpr (requires { aMessage.ExpectedOwnershipEpoch = uint32_t{}; })
        aMessage.ExpectedOwnershipEpoch = 0x87654321u;
    if constexpr (requires { aMessage.OwnerPlayerId = uint32_t{}; })
        aMessage.OwnerPlayerId = 0x1234ABCDu;
    if constexpr (requires { aMessage.RiderId = uint32_t{}; })
        aMessage.RiderId = 17;
    if constexpr (requires { aMessage.MountId = uint32_t{}; })
        aMessage.MountId = 29;
    if constexpr (requires { aMessage.RiderOwnershipEpoch = uint32_t{}; })
        aMessage.RiderOwnershipEpoch = 0xA1B2C3D4u;
    if constexpr (requires { aMessage.MountOwnershipEpoch = uint32_t{}; })
        aMessage.MountOwnershipEpoch = 0x1B2C3D4Eu;
    if constexpr (requires { aMessage.LeveledNpcPickId = GameId{}; })
        aMessage.LeveledNpcPickId = GameId(4, 0x556677);
    if constexpr (requires { aMessage.SceneId = GameId{}; })
        aMessage.SceneId = GameId(2, 0x123456);
    if constexpr (requires { aMessage.ScenePhase = uint32_t{}; })
        aMessage.ScenePhase = 0xF1234567u;
    if constexpr (requires { aMessage.SoundFilename = "intake-scene.fuz"; })
        aMessage.SoundFilename = "intake-scene.fuz";
    if constexpr (requires { aMessage.Text = "intake subtitle"; })
        aMessage.Text = "intake subtitle";
    if constexpr (requires { aMessage.Reason = OwnershipReleaseReason::DeclineGrant; })
        aMessage.Reason = OwnershipReleaseReason::DeclineGrant;
    if constexpr (requires { aMessage.CurrentActorData; })
    {
        aMessage.CurrentActorData.InitialActorValues.ActorValuesList[24] = 125.5f;
        aMessage.CurrentActorData.InitialActorValues.ActorMaxValuesList[24] = 150.0f;
        Inventory::Entry entry;
        entry.BaseId = GameId(3, 0x112233);
        entry.Count = 3;
        aMessage.CurrentActorData.InitialInventory.Entries.push_back(entry);
        aMessage.CurrentActorData.IsDead = true;
        aMessage.CurrentActorData.IsWeaponDrawn = true;
    }
}

template <class T, class TFactory>
void CheckFactoryEnvelope()
{
    CAPTURE(static_cast<uint32_t>(T::Opcode));
    T message{};
    PopulateIntakeEnvelope(message);
    TiltedPhoques::Buffer buffer(1024 * 1024);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    message.Serialize(writer);
    TiltedPhoques::Buffer exact(buffer.GetData(), writer.Size());
    TiltedPhoques::Buffer::Reader reader(&exact);
    auto decoded = TFactory{}.Extract(reader);
    REQUIRE(decoded);
    REQUIRE(decoded->GetOpcode() == T::Opcode);
    const auto* typed = dynamic_cast<const T*>(decoded.get());
    REQUIRE(typed);
    REQUIRE(*typed == message);
    if constexpr (requires { message.CurrentActorData; })
    {
        // AssignCharacterRequest's general equality omits this serialized field.
        REQUIRE(typed->CurrentActorData == message.CurrentActorData);
    }
}
}

TEST_CASE("Active client factories round-trip populated intake ownership and scene envelopes", "[encoding.intake]")
{
    size_t roundTrips = 0;
    size_t explicitFixtures = 0;
    ClientMessageFactory::Visit([&](auto aType) {
        using T = typename decltype(aType)::Type;
        if constexpr (requires(T message) { message.IsValid; })
        {
            // Three canonical messages have valid identity fixtures in the
            // protocol suite; the populated action codec has its own fixtures.
            ++explicitFixtures;
        }
        else
        {
            CheckFactoryEnvelope<T, ClientMessageFactory>();
            ++roundTrips;
        }
        return false;
    });
    REQUIRE(roundTrips == 51);
    REQUIRE(explicitFixtures == 4);
}

TEST_CASE("Active server factories round-trip populated intake ownership and scene envelopes", "[encoding.intake]")
{
    size_t roundTrips = 0;
    size_t explicitFixtures = 0;
    ServerMessageFactory::Visit([&](auto aType) {
        using T = typename decltype(aType)::Type;
        if constexpr (requires(T message) { message.IsValid; })
        {
            // Canonical fixtures are in the protocol/coordinator suites;
            // nonempty bounded action fixtures are in upstream_protocol_intake.
            ++explicitFixtures;
        }
        else
        {
            CheckFactoryEnvelope<T, ServerMessageFactory>();
            ++roundTrips;
        }
        return false;
    });
    REQUIRE(roundTrips == 54);
    REQUIRE(explicitFixtures == 4);
}
