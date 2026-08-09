#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

#include <filesystem>

namespace
{
std::filesystem::path s_executablePath;
}

const std::filesystem::path& GetTPTestsExecutablePath() noexcept
{
    return s_executablePath;
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
