#include <Misc/PartyQuestSkyrimRuntimeIdentityResolver.h>

#include <TiltedCore/VersionDb.h>

PartyQuestSkyrimRuntimeIdentityAuthorization
PartyQuestSkyrimRuntimeIdentityResolver::ResolveCurrent() noexcept
{
    try
    {
        auto& versionDb = tiltedPhoques::VersionDb::Get();
        if (!versionDb.IsSupported("SkyrimSE.exe"))
            return {};

        PartyQuestSkyrimRuntimeVersion runtimeVersion{};
        if (!PartyQuestSkyrimRuntimeVersion::TryParse(
                versionDb.GetLoadedVersionString(), runtimeVersion))
        {
            return {};
        }

        // IsSupported() is the existing project gate that binds VersionDb to
        // SkyrimSE.exe. The constructor is private so no caller can substitute
        // its own path/version while retaining this process-local capability.
        return PartyQuestSkyrimRuntimeIdentityAuthorization(
            runtimeVersion,
            true,
            true);
    }
    catch (...)
    {
        // Runtime identity is a safety prerequisite. Any unexpected VersionDb
        // failure is therefore unsupported rather than recoverable authority.
        return {};
    }
}
