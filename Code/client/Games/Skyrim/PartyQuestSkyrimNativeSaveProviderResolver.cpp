#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/PartyQuestSkyrimNativeSaveProviderResolver.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <VersionDb.h>

#include <array>
#include <cstring>
#include <limits>

namespace
{
constexpr wchar_t kProviderModule[] = L"skse64_1_6_1170.dll";
constexpr char kProviderDescriptorExport[] =
    "PartyQuestSKSE_GetSaveProviderDescriptor";

using TGetProviderDescriptor = bool(
    PartyQuestNativeSaveProviderDescriptor*,
    uint32_t);

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
    std::wstring buffer(512u, L'\0');
    for (;;)
    {
        const DWORD length = ::GetModuleFileNameW(
            aModule,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (length == 0)
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

    std::wstring buffer(512u, L'\0');
    bool success = false;
    for (;;)
    {
        const DWORD length = ::GetFinalPathNameByHandleW(
            file,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (length == 0)
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
        acFirst.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
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

[[nodiscard]] bool ExportBelongsToModule(
    HMODULE aModule,
    const void* apExport) noexcept
{
    if (!aModule || !apExport)
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (::VirtualQuery(apExport, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.AllocationBase != aModule || memory.State != MEM_COMMIT ||
        memory.Type != MEM_IMAGE ||
        (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
             PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0)
    {
        return false;
    }

    __try
    {
        const auto* base = reinterpret_cast<const std::byte*>(aModule);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
            dos->e_lfanew > 0x100000)
        {
            return false;
        }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
            base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt->OptionalHeader.SizeOfImage == 0)
        {
            return false;
        }

        const auto* address = reinterpret_cast<const std::byte*>(apExport);
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
    TGetProviderDescriptor* apGetDescriptor,
    PartyQuestNativeSaveProviderDescriptor* apDescriptor) noexcept
{
    try
    {
        return apGetDescriptor(
            apDescriptor,
            static_cast<uint32_t>(sizeof(*apDescriptor)));
    }
    catch (...)
    {
        return false;
    }
}

[[nodiscard]] bool ReadDescriptorSafely(
    TGetProviderDescriptor* apGetDescriptor,
    PartyQuestNativeSaveProviderDescriptor& aDescriptor) noexcept
{
    __try
    {
        return CallDescriptorCpp(apGetDescriptor, &aDescriptor);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
} // namespace

PartyQuestSkyrimNativeSaveProviderResolveResult
PartyQuestSkyrimNativeSaveProviderResolver::ResolveAndRegister(
    const std::filesystem::path& acTrustedGameDirectory,
    PartyQuestNativeSaveProviderRegistration& aRegistration) noexcept
{
    PartyQuestSkyrimNativeSaveProviderResolveResult result;
    if (acTrustedGameDirectory.empty() ||
        !acTrustedGameDirectory.is_absolute())
    {
        result.Status = PartyQuestSkyrimNativeSaveProviderResolveStatus::
            InvalidExpectedDirectory;
        return result;
    }

    HMODULE module = nullptr;
    if (!::GetModuleHandleExW(0u, kProviderModule, &module) || !module)
    {
        result.Status = PartyQuestSkyrimNativeSaveProviderResolveStatus::
            ProviderUnavailable;
        return result;
    }
    const ScopedModuleReference moduleReference(module);

    std::filesystem::path loadedPath;
    std::wstring loadedFinalPath;
    std::wstring expectedFinalPath;
    FILE_ID_INFO loadedIdentity{};
    FILE_ID_INFO expectedIdentity{};
    const auto expectedPath = acTrustedGameDirectory / kProviderModule;
    if (!GetModulePath(module, loadedPath) ||
        !GetFinalPath(loadedPath, loadedFinalPath) ||
        !GetFinalPath(expectedPath, expectedFinalPath) ||
        !GetFileIdentity(loadedPath, loadedIdentity) ||
        !GetFileIdentity(expectedPath, expectedIdentity))
    {
        result.Status = PartyQuestSkyrimNativeSaveProviderResolveStatus::
            ProviderPathUnavailable;
        return result;
    }
    if (!EqualPath(loadedFinalPath, expectedFinalPath) ||
        !EqualFileIdentity(loadedIdentity, expectedIdentity))
    {
        result.Status = PartyQuestSkyrimNativeSaveProviderResolveStatus::
            UnexpectedProviderPath;
        return result;
    }

    const auto getDescriptor = reinterpret_cast<TGetProviderDescriptor*>(
        ::GetProcAddress(module, kProviderDescriptorExport));
    if (!getDescriptor)
    {
        result.Status = PartyQuestSkyrimNativeSaveProviderResolveStatus::
            RequiredExportMissing;
        return result;
    }
    if (!ExportBelongsToModule(module, getDescriptor))
    {
        result.Status = PartyQuestSkyrimNativeSaveProviderResolveStatus::
            InvalidExportAddress;
        return result;
    }

    const auto& versionDb = VersionDb::Get();
    if (!versionDb.IsLoaded())
    {
        result.Status = PartyQuestSkyrimNativeSaveProviderResolveStatus::
            RuntimeDatabaseUnavailable;
        return result;
    }
    int major = 0;
    int minor = 0;
    int patch = 0;
    int build = 0;
    versionDb.GetLoadedVersion(major, minor, patch, build);
    if (major != 1 || minor != 6 || patch != 1170 || build != 0)
    {
        result.Status = PartyQuestSkyrimNativeSaveProviderResolveStatus::
            UnsupportedRuntime;
        return result;
    }

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const uint64_t generation = fence.GetGeneration();
    auto lease = fence.TryAcquire(generation);
    if (!lease || !lease->IsValid())
    {
        result.Status = PartyQuestSkyrimNativeSaveProviderResolveStatus::
            GenerationUnavailable;
        return result;
    }

    PartyQuestNativeSaveProviderDescriptor descriptor{};
    if (!ReadDescriptorSafely(getDescriptor, descriptor))
    {
        result.Status = PartyQuestSkyrimNativeSaveProviderResolveStatus::
            ProviderReadFailed;
        return result;
    }
    if (!PartyQuestNativeSaveProviderPolicy::IsApprovedDescriptor(descriptor))
    {
        result.Status = PartyQuestSkyrimNativeSaveProviderResolveStatus::
            ProviderRejected;
        return result;
    }

    auto registered =
        aRegistration.RegisterAuthenticated(descriptor, generation);
    result.RegistrationStatus = registered.Status;
    result.Token = std::move(registered.Token);
    if (result.RegistrationStatus !=
            PartyQuestNativeSaveProviderRegistrationStatus::Registered ||
        !result.Token || !result.Token->IsValid())
    {
        result.Status = PartyQuestSkyrimNativeSaveProviderResolveStatus::
            RegistrationRejected;
        return result;
    }

    // Do not permanently pin a compatible but unusable provider when the
    // registration domain is already occupied. Pin only after exact authority
    // has been issued, while the scoped reference still prevents unloading.
    HMODULE pinnedModule = nullptr;
    if (!::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(getDescriptor),
            &pinnedModule) ||
        pinnedModule != module)
    {
        result.RegistrationStatus =
            aRegistration.Invalidate(*result.Token);
        result.Token.reset();
        result.Status = PartyQuestSkyrimNativeSaveProviderResolveStatus::
            ProviderPinFailed;
        return result;
    }

    result.Status =
        PartyQuestSkyrimNativeSaveProviderResolveStatus::Registered;
    return result;
}
