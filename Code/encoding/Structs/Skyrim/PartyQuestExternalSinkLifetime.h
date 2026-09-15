#pragma once

/**
 * Releases one externally-owned event registration before the sink object can
 * be destroyed. The dispatcher pointer doubles as exact registration state, so
 * teardown is idempotent and never re-resolves an engine singleton.
 */
template <class TDispatcher, class TSink>
void PartyQuestReleaseExternalSink(
    TDispatcher*& apRegisteredDispatcher,
    TSink* apSink) noexcept
{
    if (!apRegisteredDispatcher || !apSink)
        return;

    apRegisteredDispatcher->UnRegisterSink(apSink);
    apRegisteredDispatcher = nullptr;
}
