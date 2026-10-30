#pragma once

#include "Message.h"
#include <Structs/ActionEventsCodec.h>

/**
 * @brief Animation actions relayed as soon as the owner's RequestActionEvents arrives. Sent reliably.
 */
struct NotifyActionEvents final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyActionEvents;

    NotifyActionEvents()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyActionEvents& acRhs) const noexcept { return Actions == acRhs.Actions && GetOpcode() == acRhs.GetOpcode(); }

    // Server id -> actions in the order they were performed
    ActionEventsCodec::ActionsByActor Actions{};
    bool IsValid{true}; // Local decoder status; never serialized.
};
