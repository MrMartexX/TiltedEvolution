#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

#include "TPTestsSubprocess.h"

#include <cstdlib>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <process.h>
#endif

namespace
{
std::filesystem::path s_executablePath;
}

const std::filesystem::path& GetTPTestsExecutablePath() noexcept
{
    return s_executablePath;
}

int RunTPTestsSubprocess(std::string_view aTestName)
{
    if (s_executablePath.empty() || aTestName.empty())
        return -1;

#ifdef _WIN32
    // Avoid cmd.exe entirely. The Windows CRT still formats a command line, so
    // preserve the Catch selector as one argument with explicit quotes.
    const std::wstring testName(aTestName.begin(), aTestName.end());
    const std::wstring quotedTestName = L"\"" + testName + L"\"";
    return static_cast<int>(_wspawnl(
        _P_WAIT,
        s_executablePath.c_str(),
        s_executablePath.c_str(),
        quotedTestName.c_str(),
        L"--reporter",
        L"compact",
        static_cast<const wchar_t*>(nullptr)));
#else
    const std::string command =
        "\"" + s_executablePath.string() + "\" \"" +
        std::string(aTestName) + "\" --reporter compact";
    return std::system(command.c_str());
#endif
}

int main(int argc, char* argv[])
{
    if (argc > 0 && argv[0])
    {
        std::error_code ec;
        s_executablePath = std::filesystem::absolute(argv[0], ec);
        if (ec)
            s_executablePath = argv[0];
    }

    return Catch::Session().run(argc, argv);
}
