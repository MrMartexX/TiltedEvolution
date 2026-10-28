#include <Structs/ActionEventsCodec.h>
#include <Messages/RequestActionEvents.h>
#include <TiltedCore/Serialization.hpp>
#include <catch2/catch.hpp>
#include <limits>

using TiltedPhoques::Buffer;
using TiltedPhoques::Serialization;

TEST_CASE("Action codec enforces cumulative budgets on otherwise valid payloads", "[actions.codec]")
{
    RequestActionEvents request;
    SECTION("total actions")
    {
        for (uint32_t id = 0; id < 5; ++id)
            request.Actions[id].resize(ActionEventsCodec::MaximumActionsPerActor);
    }
    SECTION("total animation variables")
    {
        ActionEvent action;
        action.Variables.Booleans.resize(ActionEventsCodec::MaximumVariablesPerAction, true);
        for (uint32_t id = 0; id < 9; ++id)
            request.Actions[id] = {action};
    }
    SECTION("individual string")
    {
        ActionEvent action;
        action.EventName.assign(ActionEventsCodec::MaximumStringBytes + 1, 'x');
        request.Actions[1] = {action};
    }
    SECTION("total strings")
    {
        ActionEvent action;
        action.EventName.assign(ActionEventsCodec::MaximumStringBytes, 'y');
        request.Actions[1].assign(17, action);
    }
    Buffer buffer(128 * 1024);
    Buffer::Writer writer(&buffer);
    request.SerializeRaw(writer);
    Buffer exact(buffer.GetData(), writer.Size());
    Buffer::Reader reader(&exact);
    RequestActionEvents incoming;
    incoming.DeserializeRaw(reader);
    REQUIRE_FALSE(incoming.IsValid);
    REQUIRE(incoming.Actions.empty());
}

TEST_CASE("Action codec rejects malformed nested masks and narrowing", "[actions.codec]")
{
    Buffer buffer(256);
    Buffer::Writer writer(&buffer);
    Serialization::WriteVarInt(writer, 1); Serialization::WriteVarInt(writer, 5); Serialization::WriteVarInt(writer, 1);
    SECTION("narrowed action id")
    {
        writer.WriteBits(1, 8); Serialization::WriteVarInt(writer, 0);
        Serialization::WriteVarInt(writer, uint64_t{1} << 32);
    }
    SECTION("wrong variable mask length")
    {
        writer.WriteBits(128, 8); Serialization::WriteVarInt(writer, 0);
        Serialization::WriteVarInt(writer, 3); Serialization::WriteVarInt(writer, 0); Serialization::WriteVarInt(writer, 0);
        Serialization::WriteVarInt(writer, 0);
    }
    Buffer exact(buffer.GetData(), writer.Size());
    Buffer::Reader reader(&exact);
    ActionEventsCodec::ActionsByActor result;
    REQUIRE_FALSE(ActionEventsCodec::Decode(reader, result));
    REQUIRE(result.empty());
}

TEST_CASE("Action codec rejects malformed counts and duplicate actor identity atomically", "[actions.codec]")
{
    Buffer buffer(256);
    Buffer::Writer writer(&buffer);
    SECTION("too many actors") { Serialization::WriteVarInt(writer, ActionEventsCodec::MaximumActors + 1); }
    SECTION("oversized actor id")
    {
        Serialization::WriteVarInt(writer, 1);
        Serialization::WriteVarInt(writer, uint64_t{1} << 32);
    }
    SECTION("too many actions for actor")
    {
        Serialization::WriteVarInt(writer, 1); Serialization::WriteVarInt(writer, 5);
        Serialization::WriteVarInt(writer, ActionEventsCodec::MaximumActionsPerActor + 1);
    }
    SECTION("duplicate actor")
    {
        Serialization::WriteVarInt(writer, 2);
        for (int i = 0; i < 2; ++i)
        {
            Serialization::WriteVarInt(writer, 5);
            Serialization::WriteVarInt(writer, 0);
        }
    }
    SECTION("overflow varint")
    {
        for (int i = 0; i < 10; ++i) { writer.WriteBits(127, 7); writer.WriteBits(1, 1); }
    }
    SECTION("oversized variables")
    {
        Serialization::WriteVarInt(writer, 1); Serialization::WriteVarInt(writer, 5); Serialization::WriteVarInt(writer, 1);
        writer.WriteBits(128, 8); Serialization::WriteVarInt(writer, 0);
        Serialization::WriteVarInt(writer, ActionEventsCodec::MaximumVariablesPerAction + 1);
        Serialization::WriteVarInt(writer, 0); Serialization::WriteVarInt(writer, 0);
    }
    ActionEventsCodec::ActionsByActor published;
    published[9] = {ActionEvent{}};
    Buffer exact(buffer.GetData(), writer.Size());
    Buffer::Reader reader(&exact);
    REQUIRE_FALSE(ActionEventsCodec::Decode(reader, published));
    REQUIRE(published.size() == 1);
    REQUIRE(published.count(9) == 1);
}

TEST_CASE("Action codec rejects every byte truncation of an uncached action", "[actions.codec]")
{
    ActionEvent action;
    action.Tick = 12345;
    action.EventName = "truncation-action";
    action.Variables.Booleans = {true, false, true};
    action.Variables.Integers = {4000};
    Buffer buffer(1024);
    Buffer::Writer writer(&buffer);
    Serialization::WriteVarInt(writer, 1); Serialization::WriteVarInt(writer, 5); Serialization::WriteVarInt(writer, 1);
    action.GenerateDifferential(ActionEvent{}, writer);
    for (size_t size = 0; size < writer.Size(); ++size)
    {
        Buffer truncated(buffer.GetData(), size);
        Buffer::Reader reader(&truncated);
        ActionEventsCodec::ActionsByActor result;
        REQUIRE_FALSE(ActionEventsCodec::Decode(reader, result));
        REQUIRE(result.empty());
    }
}

TEST_CASE("Action codec rejects absent cached strings without engine assertions", "[actions.codec]")
{
    Buffer buffer(64);
    Buffer::Writer writer(&buffer);
    Serialization::WriteVarInt(writer, 1); Serialization::WriteVarInt(writer, 5); Serialization::WriteVarInt(writer, 1);
    writer.WriteBits(32, 8); Serialization::WriteVarInt(writer, 0);
    writer.WriteBits(1, 1); Serialization::WriteVarInt(writer, std::numeric_limits<uint32_t>::max());
    Buffer exact(buffer.GetData(), writer.Size());
    Buffer::Reader reader(&exact);
    ActionEventsCodec::ActionsByActor result;
    REQUIRE_FALSE(ActionEventsCodec::Decode(reader, result));
    REQUIRE(result.empty());
}
