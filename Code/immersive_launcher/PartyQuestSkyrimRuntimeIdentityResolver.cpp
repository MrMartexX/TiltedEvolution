#include "Launcher.h"

#include <Windows.h>
#include <bcrypt.h>

#include <client/VersionDb.h>
#include <Structs/Skyrim/PartyQuestSkyrimPapyrusRuntimeProfileResolver.h>

#include <array>
#include <fstream>
#include <mutex>
#include <vector>

namespace
{
bool TryHashExecutable(
    const std::filesystem::path& acPath,
    PartyQuestSkyrimExecutableIdentity& aOut) noexcept
{
    try
    {
        BCRYPT_ALG_HANDLE algorithm{};
        BCRYPT_HASH_HANDLE hash{};
        std::vector<uint8_t> hashObject;
        const auto cleanup = [&]() noexcept {
            if (hash)
                BCryptDestroyHash(hash);
            if (algorithm)
                BCryptCloseAlgorithmProvider(algorithm, 0);
        };

        if (BCryptOpenAlgorithmProvider(
                &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            return false;

        ULONG objectLength = 0;
        ULONG resultLength = 0;
        if (BCryptGetProperty(
                algorithm,
                BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&objectLength),
                sizeof(objectLength),
                &resultLength,
                0) < 0 ||
            resultLength != sizeof(objectLength) || objectLength == 0)
        {
            cleanup();
            return false;
        }

        hashObject.resize(objectLength);
        if (BCryptCreateHash(
                algorithm,
                &hash,
                hashObject.data(),
                static_cast<ULONG>(hashObject.size()),
                nullptr,
                0,
                0) < 0)
        {
            cleanup();
            return false;
        }

        std::ifstream input(acPath, std::ios::binary);
        if (!input)
        {
            cleanup();
            return false;
        }

        std::array<uint8_t, 64 * 1024> buffer{};
        while (input)
        {
            input.read(
                reinterpret_cast<char*>(buffer.data()),
                static_cast<std::streamsize>(buffer.size()));
            const auto count = input.gcount();
            if (count > 0 && BCryptHashData(
                    hash,
                    buffer.data(),
                    static_cast<ULONG>(count),
                    0) < 0)
            {
                cleanup();
                return false;
            }
        }
        if (!input.eof())
        {
            cleanup();
            return false;
        }

        PartyQuestSkyrimExecutableIdentity candidate{};
        const NTSTATUS status = BCryptFinishHash(
            hash,
            candidate.Sha256.data(),
            static_cast<ULONG>(candidate.Sha256.size()),
            0);
        cleanup();
        if (status < 0 || !candidate.IsValid())
            return false;

        aOut = candidate;
        return true;
    }
    catch (...)
    {
        return false;
    }
}
} // namespace

PartyQuestSkyrimRuntimeIdentityAuthorization
PartyQuestSkyrimRuntimeIdentityResolver::Resolve() noexcept
{
    auto* pLaunchContext = launcher::GetLaunchContext();
    if (!pLaunchContext || !pLaunchContext->GetLoaded())
        return {};

    static std::once_flag executableIdentityOnce;
    static PartyQuestSkyrimExecutableIdentity executableIdentity{};
    std::call_once(executableIdentityOnce, [&]() noexcept {
        (void)TryHashExecutable(pLaunchContext->exePath, executableIdentity);
    });
    if (!executableIdentity.IsValid())
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
        true,
        executableIdentity);
}
