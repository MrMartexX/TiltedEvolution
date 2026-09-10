#include <Structs/Skyrim/PartyQuestReplicaWorkspaceLease.h>
#include <Structs/Skyrim/PartyQuestReplicaFiles.h>
#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <cerrno>
#include <optional>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

struct PartyQuestReplicaWorkspaceLeaseState
{
    static constexpr intptr_t InvalidHandle = -1;

    intptr_t NativeHandle{InvalidHandle};
    std::filesystem::path LockPath;
    std::filesystem::path PlayerDirectory;
    PartyQuestCampaignId CampaignId;
    PartyQuestPlayerProfileId PlayerProfileId;

    ~PartyQuestReplicaWorkspaceLeaseState() noexcept
    {
        if (NativeHandle == InvalidHandle)
            return;

#if defined(_WIN32)
        CloseHandle(reinterpret_cast<HANDLE>(NativeHandle));
#else
        flock(static_cast<int>(NativeHandle), LOCK_UN);
        close(static_cast<int>(NativeHandle));
#endif
        NativeHandle = InvalidHandle;
    }
};

namespace
{
bool IsMissingError(const std::error_code& acError) noexcept
{
    return acError == std::errc::no_such_file_or_directory ||
        acError == std::errc::not_a_directory;
}

std::optional<std::filesystem::path> PrepareConfinedMetadataDirectory(
    const PartyQuestCoopSavePaths& acPaths) noexcept
{
    try
    {
        std::error_code ec;
        const auto root = std::filesystem::absolute(acPaths.Root, ec).lexically_normal();
        if (ec || root.empty())
            return std::nullopt;

        ec.clear();
        const auto metadata =
            std::filesystem::absolute(acPaths.MetadataDirectory, ec).lexically_normal();
        if (ec || metadata.empty())
            return std::nullopt;

        const auto relative = metadata.lexically_relative(root).lexically_normal();
        if (!PartyQuestReplicaFilePlanner::IsSafeRelativePath(relative))
            return std::nullopt;

        std::filesystem::create_directories(root, ec);
        if (ec)
            return std::nullopt;

        ec.clear();
        const auto canonicalRoot = std::filesystem::weakly_canonical(root, ec);
        if (ec || canonicalRoot.empty())
            return std::nullopt;

        auto current = root;
        auto expectedCanonical = canonicalRoot;
        for (const auto& component : relative)
        {
            current /= component;
            expectedCanonical /= component;

            ec.clear();
            auto status = std::filesystem::symlink_status(current, ec);
            if (status.type() == std::filesystem::file_type::not_found || IsMissingError(ec))
            {
                ec.clear();
                if (!std::filesystem::create_directory(current, ec) || ec)
                    return std::nullopt;
                ec.clear();
                status = std::filesystem::symlink_status(current, ec);
            }
            if (ec || std::filesystem::is_symlink(status) ||
                !std::filesystem::is_directory(status))
            {
                return std::nullopt;
            }

            ec.clear();
            const auto canonicalCurrent = std::filesystem::weakly_canonical(current, ec);
            if (ec || canonicalCurrent.empty() ||
                canonicalCurrent != expectedCanonical.lexically_normal())
            {
                return std::nullopt;
            }
        }

        return expectedCanonical.lexically_normal();
    }
    catch (...)
    {
        return std::nullopt;
    }
}

bool StateProtects(
    const PartyQuestReplicaWorkspaceLeaseState* apState,
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId) noexcept
{
    if (!apState ||
        apState->NativeHandle == PartyQuestReplicaWorkspaceLeaseState::InvalidHandle ||
        acCampaignId != apState->CampaignId ||
        acPlayerProfileId != apState->PlayerProfileId ||
        !PartyQuestCoopSaveLayout::Matches(
            acPaths,
            acCampaignId,
            acPlayerProfileId))
    {
        return false;
    }

    try
    {
        std::error_code ec;
        const auto canonicalPlayer = std::filesystem::weakly_canonical(
            acPaths.PlayerDirectory, ec);
        return !ec && !canonicalPlayer.empty() &&
            canonicalPlayer.lexically_normal() == apState->PlayerDirectory;
    }
    catch (...)
    {
        return false;
    }
}
} // namespace

bool PartyQuestReplicaWorkspacePublicationCapability::Protects(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId) const noexcept
{
    return StateProtects(
        m_state.get(),
        acPaths,
        acCampaignId,
        acPlayerProfileId);
}

bool PartyQuestReplicaWorkspacePublicationCapability::PreparePowerLossDurableRuntimeNamespace(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId) const noexcept
{
    if (!Protects(acPaths, acCampaignId, acPlayerProfileId))
        return false;

    if (PartyQuestStableStorage::EnsureDirectoryTreeDurably(
            acPaths.MetadataDirectory) != PartyQuestStableStorageStatus::Success)
    {
        return false;
    }

    return PartyQuestStableStorage::EnsureDirectoryTreeDurably(
               acPaths.SidecarsDirectory) ==
        PartyQuestStableStorageStatus::Success;
}

PartyQuestReplicaWorkspaceLease::~PartyQuestReplicaWorkspaceLease() noexcept
{
    Release();
}

PartyQuestReplicaWorkspaceLease::PartyQuestReplicaWorkspaceLease(
    PartyQuestReplicaWorkspaceLease&& aRhs) noexcept
    : m_state(std::move(aRhs.m_state))
{
}

PartyQuestReplicaWorkspaceLease& PartyQuestReplicaWorkspaceLease::operator=(
    PartyQuestReplicaWorkspaceLease&& aRhs) noexcept
{
    if (this == &aRhs)
        return *this;

    Release();
    m_state = std::move(aRhs.m_state);
    return *this;
}

PartyQuestReplicaWorkspaceLeaseStatus PartyQuestReplicaWorkspaceLease::Acquire(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId) noexcept
{
    try
    {
        if (IsHeld())
            return PartyQuestReplicaWorkspaceLeaseStatus::Busy;
        if (!acCampaignId.IsValid() || !acPlayerProfileId.IsValid())
            return PartyQuestReplicaWorkspaceLeaseStatus::InvalidIdentity;
        if (!PartyQuestCoopSaveLayout::Matches(
                acPaths,
                acCampaignId,
                acPlayerProfileId))
        {
            return PartyQuestReplicaWorkspaceLeaseStatus::InvalidLayout;
        }

        const auto metadata = PrepareConfinedMetadataDirectory(acPaths);
        if (!metadata)
            return PartyQuestReplicaWorkspaceLeaseStatus::InvalidNamespace;

        auto state = std::make_shared<PartyQuestReplicaWorkspaceLeaseState>();
        state->LockPath = *metadata / "party_quest_workspace.lock";
        state->PlayerDirectory = metadata->parent_path();
        state->CampaignId = acCampaignId;
        state->PlayerProfileId = acPlayerProfileId;

#if defined(_WIN32)
        const HANDLE handle = CreateFileW(
            state->LockPath.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            const DWORD error = GetLastError();
            return error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION
                ? PartyQuestReplicaWorkspaceLeaseStatus::Busy
                : PartyQuestReplicaWorkspaceLeaseStatus::IoError;
        }

        FILE_ATTRIBUTE_TAG_INFO attributes{};
        BY_HANDLE_FILE_INFORMATION basicInfo{};
        if (!GetFileInformationByHandleEx(
                handle,
                FileAttributeTagInfo,
                &attributes,
                sizeof(attributes)) ||
            !GetFileInformationByHandle(handle, &basicInfo) ||
            (attributes.FileAttributes &
                (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
            basicInfo.nNumberOfLinks != 1)
        {
            CloseHandle(handle);
            return PartyQuestReplicaWorkspaceLeaseStatus::InvalidNamespace;
        }

        state->NativeHandle = reinterpret_cast<intptr_t>(handle);
#else
        const int descriptor = open(
            state->LockPath.c_str(),
            O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW,
            S_IRUSR | S_IWUSR);
        if (descriptor < 0)
        {
            return errno == ELOOP
                ? PartyQuestReplicaWorkspaceLeaseStatus::InvalidNamespace
                : PartyQuestReplicaWorkspaceLeaseStatus::IoError;
        }

        struct stat metadataStatus{};
        if (fstat(descriptor, &metadataStatus) != 0 ||
            !S_ISREG(metadataStatus.st_mode) ||
            metadataStatus.st_nlink != 1)
        {
            close(descriptor);
            return PartyQuestReplicaWorkspaceLeaseStatus::InvalidNamespace;
        }
        if (flock(descriptor, LOCK_EX | LOCK_NB) != 0)
        {
            const int error = errno;
            close(descriptor);
            return error == EWOULDBLOCK || error == EAGAIN
                ? PartyQuestReplicaWorkspaceLeaseStatus::Busy
                : PartyQuestReplicaWorkspaceLeaseStatus::IoError;
        }

        state->NativeHandle = descriptor;
#endif

        m_state = std::move(state);
        return PartyQuestReplicaWorkspaceLeaseStatus::Acquired;
    }
    catch (...)
    {
        return PartyQuestReplicaWorkspaceLeaseStatus::IoError;
    }
}

bool PartyQuestReplicaWorkspaceLease::IsHeld() const noexcept
{
    return m_state &&
        m_state->NativeHandle != PartyQuestReplicaWorkspaceLeaseState::InvalidHandle;
}

bool PartyQuestReplicaWorkspaceLease::Protects(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId) const noexcept
{
    return StateProtects(
        m_state.get(),
        acPaths,
        acCampaignId,
        acPlayerProfileId);
}

PartyQuestReplicaWorkspacePublicationCapability
PartyQuestReplicaWorkspaceLease::CreatePublicationCapability(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId) const noexcept
{
    if (!Protects(
            acPaths,
            acCampaignId,
            acPlayerProfileId))
    {
        return {};
    }

    return PartyQuestReplicaWorkspacePublicationCapability(m_state);
}

const std::filesystem::path& PartyQuestReplicaWorkspaceLease::GetLockPath() const noexcept
{
    static const std::filesystem::path empty;
    return m_state ? m_state->LockPath : empty;
}

void PartyQuestReplicaWorkspaceLease::Release() noexcept
{
    m_state.reset();
}
