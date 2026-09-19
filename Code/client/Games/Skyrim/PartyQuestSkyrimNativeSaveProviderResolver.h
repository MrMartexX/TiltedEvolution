#pragma once

#include <Structs/Skyrim/PartyQuestNativeSaveProvider.h>
#include <Structs/Skyrim/PartyQuestNativeSaveEventTransport.h>

#include <filesystem>
#include <utility>

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

enum class PartyQuestSkyrimNativeSaveProviderPollStatus : uint8_t
{
    Applied,
    Empty,
    GenerationUnavailable,
    ProviderRejected,
    NativeCallFailed,
    NativeQueuePoisoned,
    EventRejected
};

struct PartyQuestSkyrimNativeSaveProviderPollResult final
{
    PartyQuestSkyrimNativeSaveProviderPollStatus Status{
        PartyQuestSkyrimNativeSaveProviderPollStatus::ProviderRejected};
    PartyQuestNativeSaveEventTransportResult Transport;
};

class PartyQuestSkyrimNativeSaveProviderPollCapability final
{
public:
    PartyQuestSkyrimNativeSaveProviderPollCapability() noexcept = default;
    PartyQuestSkyrimNativeSaveProviderPollCapability(
        PartyQuestSkyrimNativeSaveProviderPollCapability&&) noexcept = default;
    PartyQuestSkyrimNativeSaveProviderPollCapability& operator=(
        PartyQuestSkyrimNativeSaveProviderPollCapability&&) noexcept = default;
    PartyQuestSkyrimNativeSaveProviderPollCapability(
        const PartyQuestSkyrimNativeSaveProviderPollCapability&) = delete;
    PartyQuestSkyrimNativeSaveProviderPollCapability& operator=(
        const PartyQuestSkyrimNativeSaveProviderPollCapability&) = delete;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return m_tryDequeue != nullptr && m_token.IsValid() &&
            m_runtimeGeneration != 0u;
    }

    // The production lifecycle owner must serialize PollAndRoute and
    // Invalidate. Each binding owns one token and one contiguous sequence
    // domain; it is not a general thread-safe queue wrapper.
    [[nodiscard]] PartyQuestSkyrimNativeSaveProviderPollResult PollAndRoute(
        const PartyQuestNativeSaveProviderRegistration& acRegistration,
        const PartyQuestAsyncSaveRequestIdentity& acReservedIdentity,
        PartyQuestAsyncSaveContract& aContract,
        PartyQuestAsyncSaveFinalizationGate& aGate,
        uint64_t aNowMs) noexcept;

    [[nodiscard]] PartyQuestNativeSaveProviderRegistrationStatus Invalidate(
        PartyQuestNativeSaveProviderRegistration& aRegistration) noexcept;

private:
    friend class PartyQuestSkyrimNativeSaveProviderResolver;
    using TTryDequeue = uint32_t(void*, uint32_t);

    PartyQuestSkyrimNativeSaveProviderPollCapability(
        TTryDequeue* apTryDequeue,
        PartyQuestNativeSaveProviderToken&& aToken) noexcept
        : m_tryDequeue(apTryDequeue),
          m_token(std::move(aToken)),
          m_runtimeGeneration(m_token.GetRuntimeGeneration())
    {
    }

    TTryDequeue* m_tryDequeue{};
    PartyQuestNativeSaveProviderToken m_token;
    PartyQuestNativeSaveEventTransport m_transport;
    uint64_t m_runtimeGeneration{};
};

struct PartyQuestSkyrimNativeSaveProviderResolveResult final
{
    PartyQuestSkyrimNativeSaveProviderResolveStatus Status{
        PartyQuestSkyrimNativeSaveProviderResolveStatus::ProviderUnavailable};
    PartyQuestNativeSaveProviderRegistrationStatus RegistrationStatus{
        PartyQuestNativeSaveProviderRegistrationStatus::InvalidDescriptor};
    std::optional<PartyQuestSkyrimNativeSaveProviderPollCapability>
        PollCapability;

    [[nodiscard]] bool IsRegistered() const noexcept
    {
        return Status ==
                PartyQuestSkyrimNativeSaveProviderResolveStatus::Registered &&
            RegistrationStatus ==
                PartyQuestNativeSaveProviderRegistrationStatus::Registered &&
            PollCapability.has_value() && PollCapability->IsValid();
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
