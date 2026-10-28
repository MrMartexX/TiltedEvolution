#include <Messages/RequestActionEvents.h>
#include <Structs/ActionEventsCodec.h>
#include <TiltedCore/Serialization.hpp>

void RequestActionEvents::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Actions.size());

    for (const auto& [serverId, actions] : Actions)
    {
        Serialization::WriteVarInt(aWriter, serverId);
        Serialization::WriteVarInt(aWriter, actions.size());

        for (const auto& action : actions)
            action.GenerateDifferential(ActionEvent{}, aWriter);
    }
}

void RequestActionEvents::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);
    IsValid = ActionEventsCodec::Decode(aReader, Actions);
    if (!IsValid)
        Actions.clear();
}
