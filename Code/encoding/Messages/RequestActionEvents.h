#pragma once

#include "Message.h"
#include <Structs/ActionEventsCodec.h>

/**
 * @brief Animation actions performed by actors owned by the sender, sent reliably along with each movement snapshot.
 */
struct RequestActionEvents final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestActionEvents;

    RequestActionEvents()
        : ClientMessage(Opcode)
    {
    }

    virtual ~RequestActionEvents() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestActionEvents& acRhs) const noexcept { return Actions == acRhs.Actions && GetOpcode() == acRhs.GetOpcode(); }

    // Server id -> actions in the order they were performed
    ActionEventsCodec::ActionsByActor Actions{};
    bool IsValid{true}; // Local decoder status; never serialized.
};
