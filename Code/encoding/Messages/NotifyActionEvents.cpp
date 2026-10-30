#include <Messages/NotifyActionEvents.h>
#include <Structs/ActionEventsCodec.h>
#include <TiltedCore/Serialization.hpp>

void NotifyActionEvents::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
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

void NotifyActionEvents::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);
    IsValid = ActionEventsCodec::Decode(aReader, Actions);
    if (!IsValid)
        Actions.clear();
}
