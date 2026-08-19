#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <system_error>

PartyQuestStableStorageStatus PartyQuestStableStorage::EnsureDirectoryTreeDurably(
    const std::filesystem::path& acDirectory) noexcept
{
    if (acDirectory.empty())
        return PartyQuestStableStorageStatus::InvalidPath;

#ifdef _WIN32
    // Win32 CreateDirectory does not expose the reviewed write-through creation
    // contract used by the NTFS file publication path. Do not infer a durable
    // directory-tree capability from generic directory handles.
    return PartyQuestStableStorageStatus::Unsupported;
#else
    try
    {
        std::error_code ec;
        const auto directory =
            std::filesystem::absolute(acDirectory, ec).lexically_normal();
        if (ec || directory.empty() || !directory.is_absolute())
            return PartyQuestStableStorageStatus::InvalidPath;

        std::filesystem::path current = directory.root_path();
        if (current.empty())
            return PartyQuestStableStorageStatus::InvalidPath;

        auto rootStatus = std::filesystem::symlink_status(current, ec);
        if (ec || std::filesystem::is_symlink(rootStatus) ||
            !std::filesystem::is_directory(rootStatus))
        {
            return PartyQuestStableStorageStatus::NodeValidationFailed;
        }

        for (const auto& component : directory.relative_path())
        {
            if (component.empty() || component == "." || component == "..")
                return PartyQuestStableStorageStatus::InvalidPath;

            const auto parent = current;
            current /= component;

            ec.clear();
            auto status = std::filesystem::symlink_status(current, ec);
            const bool missing =
                status.type() == std::filesystem::file_type::not_found ||
                ec == std::errc::no_such_file_or_directory;
            if (missing)
            {
                ec.clear();
                if (!std::filesystem::create_directory(current, ec) || ec)
                    return PartyQuestStableStorageStatus::CreateDirectoryFailed;

                ec.clear();
                status = std::filesystem::symlink_status(current, ec);
            }

            if (ec || std::filesystem::is_symlink(status) ||
                !std::filesystem::is_directory(status))
            {
                return PartyQuestStableStorageStatus::NodeValidationFailed;
            }

            // Persist the child name in its parent even when the directory was
            // created by an earlier crash-resilient path, then persist the child
            // directory's own metadata before descending into it.
            const auto parentFlush = FlushDirectory(parent);
            if (parentFlush != PartyQuestStableStorageStatus::Success)
                return parentFlush;

            const auto childFlush = FlushDirectory(current);
            if (childFlush != PartyQuestStableStorageStatus::Success)
                return childFlush;
        }

        return FlushDirectory(directory);
    }
    catch (...)
    {
        return PartyQuestStableStorageStatus::InvalidPath;
    }
#endif
}
