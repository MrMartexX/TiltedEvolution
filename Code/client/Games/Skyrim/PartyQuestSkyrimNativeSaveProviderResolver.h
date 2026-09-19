#pragma once

#include <Structs/Skyrim/PartyQuestNativeSaveProvider.h>
#include <Structs/Skyrim/PartyQuestNativeSaveEventTransport.h>
#include <Structs/Skyrim/PartyQuestNativeSaveRequest.h>

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
    RegistrationRejected,
    UnexpectedFailure
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

enum class PartyQuestSkyrimNativeSaveProviderCommandStatus : uint8_t
{
    Accepted,
    InvalidRequest,
    GenerationUnavailable,
    ProviderRejected,
    NativeCallFailed,
    NativeQueuePoisoned
};

class PartyQuestSkyrimNativeSaveProviderPollCapability final
{
public:
    PartyQuestSkyrimNativeSaveProviderPollCapability() noexcept = default;
    PartyQuestSkyrimNativeSaveProviderPollCapability(
        PartyQuestSkyrimNativeSaveProviderPollCapability&&) noexcept = default;
    PartyQuestSkyrimNativeSaveProviderPollCapability& operator=(
        PartyQuestSkyrimNativeSaveProviderPollCapability&&) noexcept = delete;
    PartyQuestSkyrimNativeSaveProviderPollCapability(
        const PartyQuestSkyrimNativeSaveProviderPollCapability&) = delete;
    PartyQuestSkyrimNativeSaveProviderPollCapability& operator=(
        const PartyQuestSkyrimNativeSaveProviderPollCapability&) = delete;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return m_begin != nullptr && m_cancel != nullptr &&
            m_tryDequeue != nullptr && m_token.IsValid() &&
            m_runtimeGeneration != 0u;
    }

    // The production lifecycle owner must serialize Begin, Cancel,
    // PollAndRoute, and Invalidate. Each binding owns one token and one
    // contiguous sequence domain; it is not a general thread-safe queue
    // wrapper. In particular, this type does not provide P0-C shutdown
    // quiescence around the pinned native calls.
    [[nodiscard]] PartyQuestSkyrimNativeSaveProviderCommandStatus Begin(
        const PartyQuestNativeSaveProviderRegistration& acRegistration,
        const PartyQuestAsyncSaveRequestIdentity& acIdentity) noexcept;

    [[nodiscard]] PartyQuestSkyrimNativeSaveProviderCommandStatus Cancel(
        const PartyQuestNativeSaveProviderRegistration& acRegistration,
        uint64_t aAttemptNonce) noexcept;

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
    using TBegin = bool(const void*, uint32_t);
    using TCancel = bool(uint64_t);
    using TTryDequeue = uint32_t(void*, uint32_t);

    PartyQuestSkyrimNativeSaveProviderPollCapability(
        TBegin* apBegin,
        TCancel* apCancel,
        TTryDequeue* apTryDequeue,
        PartyQuestNativeSaveProviderToken&& aToken) noexcept
        : m_begin(apBegin),
          m_cancel(apCancel),
          m_tryDequeue(apTryDequeue),
          m_token(std::move(aToken)),
          m_runtimeGeneration(m_token.GetRuntimeGeneration())
    {
    }

    TBegin* m_begin{};
    TCancel* m_cancel{};
    TTryDequeue* m_tryDequeue{};
    PartyQuestNativeSaveProviderToken m_token;
    PartyQuestNativeSaveEventTransport m_transport;
    uint64_t m_runtimeGeneration{};
    bool m_poisoned{};
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
