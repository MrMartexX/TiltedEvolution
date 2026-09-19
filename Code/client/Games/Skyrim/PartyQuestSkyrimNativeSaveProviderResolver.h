#pragma once

#include <Structs/Skyrim/PartyQuestNativeSaveProvider.h>

#include <filesystem>

enum class PartyQuestSkyrimNativeSaveProviderResolveStatus : uint8_t
{
    Registered,
    InvalidExpectedDirectory,
    ProviderUnavailable,
    ProviderPathUnavailable,
    UnexpectedProviderPath,
    RequiredExportMissing,
    InvalidExportAddress,
    RuntimeDatabaseUnavailable,
    UnsupportedRuntime,
    GenerationUnavailable,
    ProviderReadFailed,
    ProviderRejected,
    ProviderPinFailed,
    RegistrationRejected
};

struct PartyQuestSkyrimNativeSaveProviderResolveResult final
{
    PartyQuestSkyrimNativeSaveProviderResolveStatus Status{
        PartyQuestSkyrimNativeSaveProviderResolveStatus::ProviderUnavailable};
    PartyQuestNativeSaveProviderRegistrationStatus RegistrationStatus{
        PartyQuestNativeSaveProviderRegistrationStatus::InvalidDescriptor};
    std::optional<PartyQuestNativeSaveProviderToken> Token;

    [[nodiscard]] bool IsRegistered() const noexcept
    {
        return Status ==
                PartyQuestSkyrimNativeSaveProviderResolveStatus::Registered &&
            RegistrationStatus ==
                PartyQuestNativeSaveProviderRegistrationStatus::Registered &&
            Token.has_value() && Token->IsValid();
    }
};

/**
 * Authenticates the already-loaded, exact Skyrim 1.6.1170 SKSE provider before
 * crossing PartyQuestNativeSaveProviderRegistration's trusted-loader boundary.
 *
 * The caller must supply Skyrim's trusted installation directory. This adapter
 * never searches for or loads a DLL. It resolves the loaded module's final path,
 * verifies that the descriptor export belongs to that PE image, reads it under
 * SEH and the current runtime-generation lease, pins the accepted module, and
 * only then registers the descriptor. It does not register callbacks or enable
 * save capture; those ownership/quiescence concerns remain outside this slice.
 */
class PartyQuestSkyrimNativeSaveProviderResolver final
{
public:
    [[nodiscard]] static PartyQuestSkyrimNativeSaveProviderResolveResult
    ResolveAndRegister(
        const std::filesystem::path& acTrustedGameDirectory,
        PartyQuestNativeSaveProviderRegistration& aRegistration) noexcept;
};
