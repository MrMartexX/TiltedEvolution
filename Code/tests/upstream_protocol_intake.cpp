#include <TiltedCore/Stl.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <Messages/ClientMessageFactory.h>
#include <Messages/ServerMessageFactory.h>
#include <Structs/ActionEventsCodec.h>
#include <catch2/catch.hpp>
#include <array>

TEST_CASE("Intake factories have one registration for every active stable opcode", "[encoding.intake]")
{
    static_assert(kRequestPartyQuestTransaction == 52);
    static_assert(kRequestPartyQuestReplicaReport == 53);
    static_assert(kRequestPartyQuestRepairAck == 54);
    static_assert(kRequestActionEvents == 55);
    static_assert(kNotifyPartyQuestTransactionResult == 56);
    static_assert(kNotifyPartyQuestRepairPlan == 57);
    static_assert(kNotifyPartyQuestCanonicalUpdate == 58);
    static_assert(kNotifyActionEvents == 59);
    std::array<size_t, kClientOpcodeMax> clients{};
    std::array<size_t, kServerOpcodeMax> servers{};
    ClientMessageFactory::Visit([&](auto aType) {
        using T = typename decltype(aType)::Type;
        ++clients[T::Opcode];
        return false;
    });
    ServerMessageFactory::Visit([&](auto aType) {
        using T = typename decltype(aType)::Type;
        ++servers[T::Opcode];
        return false;
    });
    for (size_t i = 0; i < clients.size(); ++i)
        REQUIRE(clients[i] == (i == kReservedCancelAssignmentRequest ? 0 : 1));
    for (size_t i = 0; i < servers.size(); ++i)
        REQUIRE(servers[i] == (i == kReservedNotifySpawnData || i == kReservedNotifyRelinquishControl ? 0 : 1));
}

TEST_CASE("Intake factories reject retired unknown and truncated opcodes", "[encoding.intake]")
{
    const ClientMessageFactory client;
    const ServerMessageFactory server;
    for (uint8_t value : {uint8_t{1}, uint8_t{56}, uint8_t{255}})
    {
        TiltedPhoques::Buffer buffer(&value, 1);
        TiltedPhoques::Buffer::Reader reader(&buffer);
        REQUIRE_FALSE(client.Extract(reader));
    }
    for (uint8_t value : {uint8_t{16}, uint8_t{44}, uint8_t{60}, uint8_t{255}})
    {
        TiltedPhoques::Buffer buffer(&value, 1);
        TiltedPhoques::Buffer::Reader reader(&buffer);
        REQUIRE_FALSE(server.Extract(reader));
    }
    TiltedPhoques::Buffer empty(0);
    TiltedPhoques::Buffer::Reader reader(&empty);
    REQUIRE_FALSE(client.Extract(reader));
    REQUIRE_FALSE(server.Extract(reader));
}

TEST_CASE("Both action factories preserve full existing differential wire content", "[encoding.intake][actions.codec]")
{
    ActionEvent action;
    action.Tick = 0x123456789ull;
    action.ActionId = 21;
    action.TargetId = 32;
    action.IdleId = 43;
    action.State1 = 54;
    action.State2 = 65;
    action.Type = 76;
    action.EventName = "intake-action";
    action.TargetEventName = "intake-target";
    action.Variables.Booleans = {true, false, true, false, false, true, true, true, false};
    action.Variables.Integers = {0, 42, 0xFFFFFFFFu};
    action.Variables.Floats = {0.0f, 1.25f, -9.5f};

    RequestActionEvents request;
    request.Actions[123] = {action, ActionEvent{}};
    TiltedPhoques::Buffer buffer(4096);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    request.Serialize(writer);
    TiltedPhoques::Buffer exact(buffer.GetData(), writer.Size());
    TiltedPhoques::Buffer::Reader reader(&exact);
    auto decoded = ClientMessageFactory{}.Extract(reader);
    REQUIRE(decoded);
    auto* incoming = dynamic_cast<RequestActionEvents*>(decoded.get());
    REQUIRE(incoming);
    REQUIRE(incoming->IsValid);
    REQUIRE(*incoming == request);

    NotifyActionEvents notify;
    notify.Actions = request.Actions;
    TiltedPhoques::Buffer::Writer serverWriter(&buffer);
    notify.Serialize(serverWriter);
    TiltedPhoques::Buffer serverExact(buffer.GetData(), serverWriter.Size());
    TiltedPhoques::Buffer::Reader serverReader(&serverExact);
    auto reply = ServerMessageFactory{}.Extract(serverReader);
    REQUIRE(reply);
    auto* outgoing = dynamic_cast<NotifyActionEvents*>(reply.get());
    REQUIRE(outgoing);
    REQUIRE(outgoing->IsValid);
    REQUIRE(*outgoing == notify);
}
