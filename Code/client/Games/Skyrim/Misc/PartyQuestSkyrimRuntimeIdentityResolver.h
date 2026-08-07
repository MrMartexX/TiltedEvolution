#pragma once

#include <Structs/Skyrim/PartyQuestSkyrimPapyrusRuntimeProfileResolver.h>

/**
 * Client-specific issuer for the process-local Skyrim runtime identity
 * capability used by equal-party authoritative Papyrus observation.
 *
 * This class accepts no caller-supplied executable path or version metadata.
 * ResolveCurrent() interrogates the already initialized VersionDb singleton and
 * fails closed unless that singleton independently reports SkyrimSE.exe as a
 * supported executable and exposes a strict four-component loaded version.
 */
class PartyQuestSkyrimRuntimeIdentityResolver final
{
public:
    [[nodiscard]] static PartyQuestSkyrimRuntimeIdentityAuthorization
    ResolveCurrent() noexcept;
};
