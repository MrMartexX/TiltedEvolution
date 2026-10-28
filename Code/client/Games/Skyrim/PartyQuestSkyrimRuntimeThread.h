#pragma once

#include <cstdint>

/**
 * Process-local affinity guard for Skyrim canonical quest mutation.
 *
 * The Skyrim Main::Update hook binds the first engine-owned main-loop thread.
 * Papyrus VMUpdate callbacks are deliberately excluded because Skyrim may run
 * them on multiple worker threads. Native mutation executors must run
 * synchronously on the bound main-loop thread. If that callback ever migrates,
 * the original binding is retained and mutation fails closed until restart.
 *
 * This is runtime confinement only. It is not durable authority and does not
 * replace the generation lease, compatibility witness, checkpoint or recovery
 * barriers.
 */
class PartyQuestSkyrimRuntimeThread final
{
public:
    [[nodiscard]] static bool ObserveCurrentUpdateThread() noexcept;
    [[nodiscard]] static bool IsCurrentUpdateThread() noexcept;
    [[nodiscard]] static uint32_t GetBoundThreadIdForDiagnostics() noexcept;
};
