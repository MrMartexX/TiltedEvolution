#pragma once

class PartyQuestRuntimeReferenceReadiness;

/** Process-lifetime Skyrim TESObjectLoadedEvent evidence source. */
void InstallPartyQuestSkyrimReferenceReadiness() noexcept;
void UninstallPartyQuestSkyrimReferenceReadiness() noexcept;

/**
 * Returns the generation-bound reference readiness tracker fed by the Skyrim
 * object-loaded event dispatcher. This is evidence only; it does not authorize
 * deferred-world execution by itself.
 */
PartyQuestRuntimeReferenceReadiness& GetPartyQuestSkyrimReferenceReadiness() noexcept;
