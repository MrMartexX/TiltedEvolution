#include <Games/Skyrim/PartyQuestSkyrimNativeLoadBridgeResolver.h>

#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>

#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

namespace
{
class ScopedModuleReference final
{
public:
    explicit ScopedModuleReference(HMODULE aModule) noexcept
        : m_module(aModule)
    {
    }

    ~ScopedModuleReference() noexcept
    {
        if (m_module)
            ::FreeLibrary(m_module);
    }

    ScopedModuleReference(const ScopedModuleReference&) = delete;
    ScopedModuleReference& operator=(const ScopedModuleReference&) = delete;

private:
    HMODULE m_module{};
};

[[nodiscard]] bool GetModulePath(
    HMODULE aModule,
    std::filesystem::path& aPath) noexcept
{
    try
    {
        std::wstring buffer(512u, L'\0');
        for (;;)
        {
            const DWORD length = ::GetModuleFileNameW(
                aModule,
                buffer.data(),
                static_cast<DWORD>(buffer.size()));
            if (length == 0u)
                return false;

            if (length < buffer.size() - 1u)
            {
                buffer.resize(length);
                aPath = std::move(buffer);
                return true;
            }

            if (buffer.size() >= 32768u)
                return false;
            buffer.resize(buffer.size() * 2u, L'\0');
        }
    }
    catch (...)
    {
        return false;
    }
}

[[nodiscard]] bool GetFinalPath(
    const std::filesystem::path& acPath,
    std::wstring& aFinalPath) noexcept
{
    const HANDLE file = ::CreateFileW(
        acPath.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    bool success = false;
    try
    {
        std::wstring buffer(512u, L'\0');
        for (;;)
        {
            const DWORD length = ::GetFinalPathNameByHandleW(
                file,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            if (length == 0u)
                break;

            if (length < buffer.size())
            {
                buffer.resize(length);
                aFinalPath = std::move(buffer);
                success = true;
                break;
            }

            if (length >= 32768u)
                break;
            buffer.resize(static_cast<size_t>(length) + 1u, L'\0');
        }
    }
    catch (...)
    {
        success = false;
    }

    ::CloseHandle(file);
    return success;
}

[[nodiscard]] bool GetFileIdentity(
    const std::filesystem::path& acPath,
    FILE_ID_INFO& aIdentity) noexcept
{
    const HANDLE file = ::CreateFileW(
        acPath.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    const bool success = ::GetFileInformationByHandleEx(
        file,
        FileIdInfo,
        &aIdentity,
        sizeof(aIdentity)) != FALSE;
    ::CloseHandle(file);
    return success;
}

[[nodiscard]] bool EqualFileIdentity(
    const FILE_ID_INFO& acFirst,
    const FILE_ID_INFO& acSecond) noexcept
{
    return acFirst.VolumeSerialNumber == acSecond.VolumeSerialNumber &&
        std::memcmp(
            acFirst.FileId.Identifier,
            acSecond.FileId.Identifier,
            sizeof(acFirst.FileId.Identifier)) == 0;
}

[[nodiscard]] bool EqualPath(
    const std::wstring& acFirst,
    const std::wstring& acSecond) noexcept
{
    if (acFirst.size() != acSecond.size() ||
        acFirst.size() >
            static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }

    return ::CompareStringOrdinal(
               acFirst.data(),
               static_cast<int>(acFirst.size()),
               acSecond.data(),
               static_cast<int>(acSecond.size()),
               TRUE) == CSTR_EQUAL;
}

template <class T>
[[nodiscard]] bool ExportBelongsToModule(
    HMODULE aModule,
    T apExport) noexcept
{
    if (!aModule || !apExport)
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (::VirtualQuery(
            reinterpret_cast<LPCVOID>(apExport),
            &memory,
            sizeof(memory)) != sizeof(memory) ||
        memory.AllocationBase != aModule ||
        memory.State != MEM_COMMIT ||
        memory.Type != MEM_IMAGE)
    {
        return false;
    }

    const DWORD protection = memory.Protect & 0xFFu;
    if (protection != PAGE_EXECUTE &&
        protection != PAGE_EXECUTE_READ &&
        protection != PAGE_EXECUTE_READWRITE &&
        protection != PAGE_EXECUTE_WRITECOPY)
    {
        return false;
    }

    __try
    {
        const auto* base =
            reinterpret_cast<const std::byte*>(aModule);
        const auto* dos =
            reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
            dos->e_lfanew <= 0 ||
            dos->e_lfanew > 0x100000)
        {
            return false;
        }

        const auto* nt =
            reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic !=
                IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt->OptionalHeader.SizeOfImage == 0u)
        {
            return false;
        }

        const auto* address =
            reinterpret_cast<const std::byte*>(apExport);
        return address >= base &&
            static_cast<size_t>(address - base) <
                nt->OptionalHeader.SizeOfImage;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

[[nodiscard]] bool CallDescriptorCpp(
    PartyQuestNativeLoadBridgeGetDescriptorExport apGetDescriptor,
    PartyQuestNativeLoadBridgeDescriptorV1& aDescriptor,
    uint32_t& aRawResult) noexcept
{
    try
    {
        aRawResult = apGetDescriptor(
            &aDescriptor,
            static_cast<uint32_t>(sizeof(aDescriptor)));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

[[nodiscard]] bool ReadDescriptorSafely(
    PartyQuestNativeLoadBridgeGetDescriptorExport apGetDescriptor,
    PartyQuestNativeLoadBridgeDescriptorV1& aDescriptor,
    uint32_t& aRawResult) noexcept
{
    __try
    {
        return CallDescriptorCpp(
            apGetDescriptor,
            aDescriptor,
            aRawResult);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

[[nodiscard]] bool RuntimeMatches(
    const PartyQuestSkyrimRuntimeVersion& acRuntime,
    const PartyQuestNativeLoadBridgeDescriptorV1& acDescriptor) noexcept
{
    return acRuntime.Major == acDescriptor.RuntimeMajor &&
        acRuntime.Minor == acDescriptor.RuntimeMinor &&
        acRuntime.Patch == acDescriptor.RuntimePatch &&
        acRuntime.Build == acDescriptor.RuntimeBuild;
}

template <class T>
[[nodiscard]] bool ProcessImageAddressAcceptedCpp(
    PartyQuestSkyrimNativeLoadBridgeImageAddressValidator apValidator,
    T apFunction) noexcept
{
    try
    {
        return apValidator &&
            apFunction &&
            apValidator(reinterpret_cast<const uint8_t*>(apFunction));
    }
    catch (...)
    {
        return false;
    }
}

template <class T>
[[nodiscard]] bool ProcessImageAddressAccepted(
    PartyQuestSkyrimNativeLoadBridgeImageAddressValidator apValidator,
    T apFunction) noexcept
{
    __try
    {
        return ProcessImageAddressAcceptedCpp(
            apValidator,
            apFunction);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
} // namespace

bool PartyQuestSkyrimNativeLoadBridgeResolver::HashFileSha256(
    const std::filesystem::path& acPath,
    std::array<uint8_t, 32>& aHash) noexcept
{
    aHash = {};

    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    std::vector<uint8_t> hashObject;

    const auto cleanup = [&]() noexcept
    {
        if (hash)
            BCryptDestroyHash(hash);
        if (algorithm)
            BCryptCloseAlgorithmProvider(algorithm, 0u);
    };

    try
    {
        if (BCryptOpenAlgorithmProvider(
                &algorithm,
                BCRYPT_SHA256_ALGORITHM,
                nullptr,
                0u) < 0)
        {
            cleanup();
            return false;
        }

        ULONG objectLength = 0u;
        ULONG resultLength = 0u;
        if (BCryptGetProperty(
                algorithm,
                BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&objectLength),
                sizeof(objectLength),
                &resultLength,
                0u) < 0 ||
            resultLength != sizeof(objectLength) ||
            objectLength == 0u)
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
                0u,
                0u) < 0)
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

        std::array<uint8_t, 64u * 1024u> buffer{};
        while (input)
        {
            input.read(
                reinterpret_cast<char*>(buffer.data()),
                static_cast<std::streamsize>(buffer.size()));
            const auto count = input.gcount();
            if (count > 0 &&
                BCryptHashData(
                    hash,
                    buffer.data(),
                    static_cast<ULONG>(count),
                    0u) < 0)
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

        if (BCryptFinishHash(
                hash,
                aHash.data(),
                static_cast<ULONG>(aHash.size()),
                0u) < 0)
        {
            aHash = {};
            cleanup();
            return false;
        }

        cleanup();
        return true;
    }
    catch (...)
    {
        cleanup();
        aHash = {};
        return false;
    }
}

PartyQuestSkyrimNativeLoadBridgeBindResult
PartyQuestSkyrimNativeLoadBridgeResolver::ResolveAndBind(
    const std::filesystem::path& acTrustedGameDirectory,
    const PartyQuestSkyrimRuntimeIdentityAuthorization& acRuntimeIdentity,
    const PartyQuestSkyrimNativeLoadBridgeSourceAuthorization& acSource,
    uint64_t aExpectedGeneration,
    PartyQuestSkyrimNativeLoadBridgeOwner& aOwner) noexcept
{
    return BindResolved(
        aOwner,
        ResolveAndPin(
            acTrustedGameDirectory,
            acRuntimeIdentity,
            acSource,
            aExpectedGeneration));
}

PartyQuestSkyrimNativeLoadBridgeBindResult
PartyQuestSkyrimNativeLoadBridgeResolver::ResolveReviewedAndBind(
    const std::filesystem::path& acTrustedGameDirectory,
    const PartyQuestSkyrimRuntimeIdentityAuthorization& acRuntimeIdentity,
    uint64_t aExpectedGeneration,
    PartyQuestSkyrimNativeLoadBridgeOwner& aOwner) noexcept
{
    const auto source =
        PartyQuestSkyrimNativeLoadBridgeSourceRegistry::Resolve(
            acRuntimeIdentity);
    return ResolveAndBind(
        acTrustedGameDirectory,
        acRuntimeIdentity,
        source,
        aExpectedGeneration,
        aOwner);
}

PartyQuestSkyrimNativeLoadBridgeBindResult
PartyQuestSkyrimNativeLoadBridgeResolver::ResolveProcessImageAndBind(
    const PartyQuestSkyrimRuntimeIdentityAuthorization& acRuntimeIdentity,
    const PartyQuestSkyrimNativeLoadBridgeSourceAuthorization& acSource,
    uint64_t aExpectedGeneration,
    PartyQuestSkyrimNativeLoadBridgeOwner& aOwner) noexcept
{
    return BindResolved(
        aOwner,
        ResolveProcessImageAndPin(
            acRuntimeIdentity,
            acSource,
            aExpectedGeneration));
}

PartyQuestSkyrimNativeLoadBridgeBindResult
PartyQuestSkyrimNativeLoadBridgeResolver::
    ResolveReviewedProcessImageAndBind(
        const PartyQuestSkyrimRuntimeIdentityAuthorization& acRuntimeIdentity,
        uint64_t aExpectedGeneration,
        PartyQuestSkyrimNativeLoadBridgeOwner& aOwner) noexcept
{
    const auto source =
        PartyQuestSkyrimNativeLoadBridgeSourceRegistry::Resolve(
            acRuntimeIdentity);
    return ResolveProcessImageAndBind(
        acRuntimeIdentity,
        source,
        aExpectedGeneration,
        aOwner);
}

PartyQuestSkyrimNativeLoadBridgeBindResult
PartyQuestSkyrimNativeLoadBridgeResolver::BindResolved(
    PartyQuestSkyrimNativeLoadBridgeOwner& aOwner,
    PartyQuestSkyrimNativeLoadBridgeResolveResult&& aResolved) noexcept
{
    PartyQuestSkyrimNativeLoadBridgeBindResult result{};
    result.ResolverStatus = aResolved.Status;
    result.LeaseStatus = aResolved.LeaseStatus;
    result.Descriptor = aResolved.Descriptor;

    if (!aResolved.IsResolved() || !aResolved.Lease)
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeBindStatus::ResolveRejected;
        return result;
    }

    result.Owner =
        aOwner.BindAuthenticated(std::move(*aResolved.Lease));
    if (result.Owner.Status ==
            PartyQuestSkyrimNativeLoadBridgeOwnerStatus::Applied &&
        result.Owner.State.Code ==
            PartyQuestNativeLoadBridgeOwnerResultCode::Bound)
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeBindStatus::Bound;
        return result;
    }

    result.Status =
        PartyQuestSkyrimNativeLoadBridgeBindStatus::OwnerRejected;
    return result;
}

PartyQuestSkyrimNativeLoadBridgeResolveResult
PartyQuestSkyrimNativeLoadBridgeResolver::ResolveProcessImageAndPin(
    const PartyQuestSkyrimRuntimeIdentityAuthorization& acRuntimeIdentity,
    const PartyQuestSkyrimNativeLoadBridgeSourceAuthorization& acSource,
    uint64_t aExpectedGeneration) noexcept
try
{
    PartyQuestSkyrimNativeLoadBridgeResolveResult result;

    if (!acRuntimeIdentity.IsVerified())
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                RuntimeIdentityRejected;
        return result;
    }

    if (!acSource.IsVerified())
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                SourceAuthorizationRejected;
        return result;
    }

    if (acSource.GetLifetimeKind() !=
        PartyQuestSkyrimNativeLoadBridgeLeaseLifetimeKind::ProcessImage)
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                SourceKindMismatch;
        return result;
    }

    if (!acRuntimeIdentity.GetRuntimeVersion().Matches(
            acSource.GetRuntimeVersion()))
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                RuntimeMismatch;
        return result;
    }

    if (!acRuntimeIdentity.GetExecutableIdentity().Matches(
            acSource.GetExecutableIdentity()))
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                ExecutableIdentityMismatch;
        return result;
    }

    if (aExpectedGeneration == 0u)
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                GenerationUnavailable;
        return result;
    }

    auto generationLease =
        PartyQuestRuntimeGenerationFence::GetProcessFence().TryAcquire(
            aExpectedGeneration);
    if (!generationLease || !generationLease->IsValid())
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                GenerationUnavailable;
        return result;
    }

    const auto validator = acSource.GetProcessImageValidator();
    const auto getDescriptor = acSource.GetProcessImageDescriptor();
    const auto reserve = acSource.GetProcessImageReserve();
    const auto cancel = acSource.GetProcessImageCancel();
    const auto poll = acSource.GetProcessImagePoll();
    const auto retire = acSource.GetProcessImageRetire();

    if (!ProcessImageAddressAccepted(validator, getDescriptor) ||
        !ProcessImageAddressAccepted(validator, reserve) ||
        !ProcessImageAddressAccepted(validator, cancel) ||
        !ProcessImageAddressAccepted(validator, poll) ||
        !ProcessImageAddressAccepted(validator, retire))
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                ProcessImageAddressRejected;
        return result;
    }

    uint32_t rawDescriptorResult =
        static_cast<uint32_t>(
            PartyQuestNativeLoadBridgeDescriptorResult::Unavailable);
    PartyQuestNativeLoadBridgeDescriptorV1 descriptor{};
    if (!ReadDescriptorSafely(
            getDescriptor,
            descriptor,
            rawDescriptorResult))
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                DescriptorCallFailed;
        return result;
    }

    if (!PartyQuestNativeLoadBridgePolicy::IsKnownDescriptorResult(
            rawDescriptorResult) ||
        rawDescriptorResult !=
            static_cast<uint32_t>(
                PartyQuestNativeLoadBridgeDescriptorResult::Available) ||
        !PartyQuestNativeLoadBridgePolicy::IsApprovedDescriptor(
            descriptor,
            acSource.GetRuntimeFingerprint()) ||
        !RuntimeMatches(
            acSource.GetRuntimeVersion(),
            descriptor))
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                DescriptorRejected;
        return result;
    }

    result.Descriptor = descriptor;

    auto processLease =
        PartyQuestSkyrimNativeLoadBridgeModuleLease::
            CreateProcessImageAuthenticated(
                aExpectedGeneration,
                acSource.GetRuntimeFingerprint(),
                validator,
                getDescriptor,
                reserve,
                cancel,
                poll,
                retire,
                result.LeaseStatus);

    if (result.LeaseStatus !=
            PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus::
                Ready ||
        !processLease.IsCallable() ||
        processLease.GetLifetimeKind() !=
            PartyQuestSkyrimNativeLoadBridgeLeaseLifetimeKind::ProcessImage)
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                ModuleLeaseRejected;
        return result;
    }

    result.Lease.emplace(std::move(processLease));
    result.Status =
        PartyQuestSkyrimNativeLoadBridgeResolveStatus::Resolved;
    return result;
}
catch (...)
{
    PartyQuestSkyrimNativeLoadBridgeResolveResult result;
    result.Status =
        PartyQuestSkyrimNativeLoadBridgeResolveStatus::
            UnexpectedFailure;
    return result;
}

PartyQuestSkyrimNativeLoadBridgeResolveResult
PartyQuestSkyrimNativeLoadBridgeResolver::ResolveAndPin(
    const std::filesystem::path& acTrustedGameDirectory,
    const PartyQuestSkyrimRuntimeIdentityAuthorization& acRuntimeIdentity,
    const PartyQuestSkyrimNativeLoadBridgeSourceAuthorization& acSource,
    uint64_t aExpectedGeneration) noexcept
try
{
    PartyQuestSkyrimNativeLoadBridgeResolveResult result;

    if (acTrustedGameDirectory.empty() ||
        !acTrustedGameDirectory.is_absolute())
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                InvalidTrustedDirectory;
        return result;
    }

    if (!acRuntimeIdentity.IsVerified())
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                RuntimeIdentityRejected;
        return result;
    }

    if (!acSource.IsVerified())
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                SourceAuthorizationRejected;
        return result;
    }

    if (acSource.GetLifetimeKind() !=
        PartyQuestSkyrimNativeLoadBridgeLeaseLifetimeKind::
            ExternalPinnedModule)
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                SourceKindMismatch;
        return result;
    }

    if (!acRuntimeIdentity.GetRuntimeVersion().Matches(
            acSource.GetRuntimeVersion()))
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                RuntimeMismatch;
        return result;
    }

    if (!acRuntimeIdentity.GetExecutableIdentity().Matches(
            acSource.GetExecutableIdentity()))
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                ExecutableIdentityMismatch;
        return result;
    }

    if (aExpectedGeneration == 0u)
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                GenerationUnavailable;
        return result;
    }

    const auto moduleFileName =
        acSource.GetRelativeModulePath().filename();
    HMODULE module = nullptr;
    if (moduleFileName.empty() ||
        !::GetModuleHandleExW(
            0u,
            moduleFileName.c_str(),
            &module) ||
        !module)
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                ModuleUnavailable;
        return result;
    }
    const ScopedModuleReference moduleReference(module);

    const auto expectedPath =
        acTrustedGameDirectory /
        acSource.GetRelativeModulePath();

    std::filesystem::path loadedPath;
    std::wstring loadedFinalPath;
    std::wstring expectedFinalPath;
    FILE_ID_INFO loadedIdentity{};
    FILE_ID_INFO expectedIdentity{};
    if (!GetModulePath(module, loadedPath) ||
        !GetFinalPath(loadedPath, loadedFinalPath) ||
        !GetFinalPath(expectedPath, expectedFinalPath) ||
        !GetFileIdentity(loadedPath, loadedIdentity) ||
        !GetFileIdentity(expectedPath, expectedIdentity))
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                ModulePathUnavailable;
        return result;
    }

    if (!EqualPath(loadedFinalPath, expectedFinalPath) ||
        !EqualFileIdentity(loadedIdentity, expectedIdentity))
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                UnexpectedModulePath;
        return result;
    }

    std::array<uint8_t, 32> actualModuleHash{};
    if (!HashFileSha256(loadedPath, actualModuleHash) ||
        actualModuleHash != acSource.GetModuleSha256())
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                ModuleHashMismatch;
        return result;
    }

    const auto getDescriptor =
        reinterpret_cast<
            PartyQuestNativeLoadBridgeGetDescriptorExport>(
                ::GetProcAddress(
                    module,
                    kPartyQuestNativeLoadBridgeGetDescriptorExport));
    const auto reserve =
        reinterpret_cast<
            PartyQuestNativeLoadBridgeReserveExport>(
                ::GetProcAddress(
                    module,
                    kPartyQuestNativeLoadBridgeReserveExport));
    const auto cancel =
        reinterpret_cast<
            PartyQuestNativeLoadBridgeCancelExport>(
                ::GetProcAddress(
                    module,
                    kPartyQuestNativeLoadBridgeCancelExport));
    const auto poll =
        reinterpret_cast<
            PartyQuestNativeLoadBridgePollExport>(
                ::GetProcAddress(
                    module,
                    kPartyQuestNativeLoadBridgePollExport));
    const auto retire =
        reinterpret_cast<
            PartyQuestNativeLoadBridgeRetireExport>(
                ::GetProcAddress(
                    module,
                    kPartyQuestNativeLoadBridgeRetireExport));

    if (!getDescriptor ||
        !reserve ||
        !cancel ||
        !poll ||
        !retire)
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                RequiredExportMissing;
        return result;
    }

    if (!ExportBelongsToModule(module, getDescriptor) ||
        !ExportBelongsToModule(module, reserve) ||
        !ExportBelongsToModule(module, cancel) ||
        !ExportBelongsToModule(module, poll) ||
        !ExportBelongsToModule(module, retire))
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                InvalidExportAddress;
        return result;
    }

    auto generationLease =
        PartyQuestRuntimeGenerationFence::GetProcessFence().TryAcquire(
            aExpectedGeneration);
    if (!generationLease || !generationLease->IsValid())
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                GenerationUnavailable;
        return result;
    }

    uint32_t rawDescriptorResult =
        static_cast<uint32_t>(
            PartyQuestNativeLoadBridgeDescriptorResult::Unavailable);
    PartyQuestNativeLoadBridgeDescriptorV1 descriptor{};
    if (!ReadDescriptorSafely(
            getDescriptor,
            descriptor,
            rawDescriptorResult))
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                DescriptorCallFailed;
        return result;
    }

    if (!PartyQuestNativeLoadBridgePolicy::IsKnownDescriptorResult(
            rawDescriptorResult) ||
        rawDescriptorResult !=
            static_cast<uint32_t>(
                PartyQuestNativeLoadBridgeDescriptorResult::Available) ||
        !PartyQuestNativeLoadBridgePolicy::IsApprovedDescriptor(
            descriptor,
            acSource.GetRuntimeFingerprint()) ||
        !RuntimeMatches(
            acSource.GetRuntimeVersion(),
            descriptor))
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                DescriptorRejected;
        return result;
    }

    result.Descriptor = descriptor;

    auto moduleLease =
        PartyQuestSkyrimNativeLoadBridgeModuleLease::CreateAuthenticated(
            module,
            aExpectedGeneration,
            acSource.GetRuntimeFingerprint(),
            getDescriptor,
            reserve,
            cancel,
            poll,
            retire,
            result.LeaseStatus);

    if (result.LeaseStatus !=
            PartyQuestSkyrimNativeLoadBridgeModuleLeaseCreateStatus::
                Ready ||
        !moduleLease.IsCallable())
    {
        result.Status =
            PartyQuestSkyrimNativeLoadBridgeResolveStatus::
                ModuleLeaseRejected;
        return result;
    }

    result.Lease.emplace(std::move(moduleLease));
    result.Status =
        PartyQuestSkyrimNativeLoadBridgeResolveStatus::Resolved;
    return result;
}
catch (...)
{
    PartyQuestSkyrimNativeLoadBridgeResolveResult result;
    result.Status =
        PartyQuestSkyrimNativeLoadBridgeResolveStatus::
            UnexpectedFailure;
    return result;
}
