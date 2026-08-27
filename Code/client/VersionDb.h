#pragma once

#include <Structs/Skyrim/SkyrimAddressLibraryDatabase.h>

#include <Windows.h>
#include <fstream>
#include <map>
#include <stdio.h>
#include <utility>
#include <vector>

#pragma comment(lib, "version.lib")

class VersionDb
{
public:
    VersionDb() { Clear(); }
    ~VersionDb() {}

    static VersionDb& Get();

private:
    std::map<unsigned long long, unsigned long long> _data;
    std::map<unsigned long long, unsigned long long> _rdata;
    int _ver[4];
    std::string _verStr;
    std::string _moduleName;
    unsigned long long _base;
    bool _loaded{false};

    static void* ToPointer(unsigned long long v) { return (void*)v; }

    static unsigned long long FromPointer(void* ptr) { return (unsigned long long)ptr; }

    static bool ParseVersionFromString(const char* ptr, int& major, int& minor, int& revision, int& build) { return sscanf_s(ptr, "%d.%d.%d.%d", &major, &minor, &revision, &build) == 4 && ((major != 1 && major != 0) || minor != 0 || revision != 0 || build != 0); }

public:
    const std::string& GetModuleName() const { return _moduleName; }
    const std::string& GetLoadedVersionString() const { return _verStr; }
    bool IsLoaded() const { return _loaded; }

    const std::map<unsigned long long, unsigned long long>& GetOffsetMap() const { return _data; }

    void* FindAddressById(unsigned long long id) const
    {
        unsigned long long b = _base;
        if (b == 0)
            return NULL;

        unsigned long long offset = 0;
        if (!FindOffsetById(id, offset))
            return NULL;

        return ToPointer(b + offset);
    }

    bool FindOffsetById(unsigned long long id, unsigned long long& result) const
    {
        auto itr = _data.find(id);
        if (itr != _data.end())
        {
            result = itr->second;
            return true;
        }
        return false;
    }

    bool FindIdByAddress(void* ptr, unsigned long long& result) const
    {
        unsigned long long b = _base;
        if (b == 0)
            return false;

        unsigned long long addr = FromPointer(ptr);
        return FindIdByOffset(addr - b, result);
    }

    bool FindIdByOffset(unsigned long long offset, unsigned long long& result) const
    {
        auto itr = _rdata.find(offset);
        if (itr == _rdata.end())
            return false;

        result = itr->second;
        return true;
    }

    bool GetExecutableVersion(int& major, int& minor, int& revision, int& build) const
    {
        TCHAR szVersionFile[MAX_PATH];
        GetModuleFileName(NULL, szVersionFile, MAX_PATH);

        DWORD verHandle = 0;
        UINT size = 0;
        LPBYTE lpBuffer = NULL;
        DWORD verSize = GetFileVersionInfoSize(szVersionFile, &verHandle);

        if (verSize != NULL)
        {
            LPSTR verData = new char[verSize];

            if (GetFileVersionInfo(szVersionFile, verHandle, verSize, verData))
            {
                {
                    char* vstr = NULL;
                    UINT vlen = 0;
                    if (VerQueryValueA(verData, "\\StringFileInfo\\040904B0\\ProductVersion", (LPVOID*)&vstr, &vlen) && vlen && vstr && *vstr)
                    {
                        if (ParseVersionFromString(vstr, major, minor, revision, build))
                        {
                            delete[] verData;
                            return true;
                        }
                    }
                }

                {
                    char* vstr = NULL;
                    UINT vlen = 0;
                    if (VerQueryValueA(verData, "\\StringFileInfo\\040904B0\\FileVersion", (LPVOID*)&vstr, &vlen) && vlen && vstr && *vstr)
                    {
                        if (ParseVersionFromString(vstr, major, minor, revision, build))
                        {
                            delete[] verData;
                            return true;
                        }
                    }
                }
            }

            delete[] verData;
        }

        return false;
    }

    void GetLoadedVersion(int& major, int& minor, int& revision, int& build) const
    {
        major = _ver[0];
        minor = _ver[1];
        revision = _ver[2];
        build = _ver[3];
    }

    void Clear()
    {
        _data.clear();
        _rdata.clear();
        for (int i = 0; i < 4; i++)
            _ver[i] = 0;
        _verStr.clear();
        _moduleName = std::string();
        _base = 0;
        _loaded = false;
    }

    bool Load(const std::filesystem::path& acGamePath, const TiltedPhoques::String& acExeVersion)
    {
        int major, minor, revision, build;

        if (!ParseVersionFromString(acExeVersion.c_str(), major, minor, revision, build))
        {
            Clear();
            return false;
        }

        return Load(acGamePath, major, minor, revision, build);
    }

    bool Load(const std::filesystem::path& acGamePath, int major, int minor, int revision, int build)
    {
        Clear();
        if (major <= 0 || minor < 0 || revision < 0 || build < 0)
            return false;

        try
        {
            char fileName[256];
            const char* prefix =
                major == 1 && minor == 5 ? "version" : "versionlib";
            _snprintf_s(
                fileName,
                sizeof(fileName),
                "%s-%d-%d-%d-%d.bin",
                prefix,
                major,
                minor,
                revision,
                build);

            const auto path = acGamePath / "Data" / "SKSE" / "Plugins" / fileName;
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.good())
                return false;

            constexpr std::streamoff kMaximumDatabaseSize = 64ll * 1024ll * 1024ll;
            const std::streamoff fileSize = file.tellg();
            if (fileSize <= 0 || fileSize > kMaximumDatabaseSize)
                return false;

            std::vector<uint8_t> bytes(static_cast<size_t>(fileSize));
            file.seekg(0, std::ios::beg);
            file.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            if (!file || file.gcount() != static_cast<std::streamsize>(bytes.size()))
                return false;

            const SkyrimAddressLibraryRuntimeVersion expectedRuntime{
                static_cast<uint32_t>(major),
                static_cast<uint32_t>(minor),
                static_cast<uint32_t>(revision),
                static_cast<uint32_t>(build)};
            SkyrimAddressLibraryImage image{};
            if (!SkyrimAddressLibraryDatabaseParser::TryParse(
                    bytes,
                    expectedRuntime,
                    "SkyrimSE.exe",
                    image) ||
                image.IdNamespace != SkyrimAddressLibraryIdNamespace::Ae ||
                image.Entries.empty())
            {
                // STR relocation callsites currently use AE-side IDs. A v1
                // database is deliberately rejected until an exact reviewed
                // SE-to-AE ID translation profile exists.
                return false;
            }

            const HMODULE handle = GetModuleHandleA(nullptr);
            if (!handle)
                return false;

            std::map<unsigned long long, unsigned long long> data;
            std::map<unsigned long long, unsigned long long> reverseData;
            for (const auto& entry : image.Entries)
            {
                data.emplace(entry.Id, entry.Offset);
                reverseData.emplace(entry.Offset, entry.Id);
            }

            char versionName[64];
            _snprintf_s(
                versionName,
                sizeof(versionName),
                "%d.%d.%d.%d",
                major,
                minor,
                revision,
                build);

            _data = std::move(data);
            _rdata = std::move(reverseData);
            _ver[0] = major;
            _ver[1] = minor;
            _ver[2] = revision;
            _ver[3] = build;
            _verStr = versionName;
            _moduleName = std::move(image.ModuleName);
            _base = reinterpret_cast<unsigned long long>(handle);
            _loaded = true;
            return true;
        }
        catch (...)
        {
            Clear();
            return false;
        }
    }

    bool DumpToTextFile(const std::string& path)
    {
        std::ofstream f = std::ofstream(path.c_str());
        if (!f.good())
            return false;

        for (auto itr = _data.begin(); itr != _data.end(); itr++)
        {
            f << std::dec;
            f << itr->first;
            f << '\t';
            f << std::hex;
            f << itr->second + 0x140000000;
            f << '\n';
        }

        return true;
    }
};

template <class T> struct VersionDbPtr
{
    VersionDbPtr(const uint32_t aId) noexcept
        : m_pPtr{nullptr}
        , m_id{aId}
    {
    }

    VersionDbPtr() = delete;
    VersionDbPtr(VersionDbPtr&) = delete;
    VersionDbPtr& operator=(VersionDbPtr&) = delete;

    operator T*() const noexcept { return Get(); }

    T* operator->() const noexcept { return Get(); }

    T* Get() const noexcept { return static_cast<T*>(GetPtr()); }

    void* GetPtr() const noexcept
    {
        if (m_pPtr == nullptr)
            m_pPtr = VersionDb::Get().FindAddressById(m_id);

        return m_pPtr;
    }

private:
    mutable void* m_pPtr;
    uint32_t m_id;
};
