#include "Launcher.h"

#include <client/VersionDb.h>
#include <Structs/Skyrim/PartyQuestSkyrimPapyrusRuntimeProfileResolver.h>

PartyQuestSkyrimRuntimeIdentityAuthorization
PartyQuestSkyrimRuntimeIdentityResolver::Resolve() noexcept
{
    const auto* pLaunchContext = launcher::GetLaunchContext();
    if (!pLaunchContext || !pLaunchContext->GetLoaded())
        return {};

    PartyQuestSkyrimRuntimeVersion mappedExecutableVersion{};
    if (!PartyQuestSkyrimRuntimeVersion::TryParse(
            pLaunchContext->Version.c_str(), mappedExecutableVersion))
    {
        return {};
    }

    const auto& versionDb = VersionDb::Get();
    if (!versionDb.IsLoaded())
        return {};

    int major = 0;
    int minor = 0;
    int patch = 0;
    int build = 0;
    versionDb.GetLoadedVersion(major, minor, patch, build);
    if (major <= 0 || minor < 0 || patch < 0 || build < 0)
        return {};

    const PartyQuestSkyrimRuntimeVersion versionDbVersion{
        static_cast<uint32_t>(major),
        static_cast<uint32_t>(minor),
        static_cast<uint32_t>(patch),
        static_cast<uint32_t>(build)};

    return ResolveTrustedState(
        mappedExecutableVersion,
        true,
        versionDbVersion,
        true);
}
