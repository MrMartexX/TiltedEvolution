#pragma once

#include <cstdint>

/**
 * Process-local affinity guard for Skyrim canonical quest mutation.
 *
 * TiltedOnlineApp::Update binds the first thread that drives World::Update.
 * Native mutation executors must run synchronously on that same thread. If the
 * application update callback ever migrates to another thread, the original
 * binding is retained and mutation fails closed until process restart.
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
