#include <steam/SteamCrypto.h>

#include <catch2/catch.hpp>

#include <cstddef>

TEST_CASE("SteamStub 3.1 layout keeps Skyrim AppId separate from DRM flags", "[launcher][steam-ceg][runtime-compatibility]")
{
    static_assert(sizeof(steam::SteamStubHeaderV31) == 240u);
    static_assert(offsetof(steam::SteamStubHeaderV31, SteamAppId) == 0x38u);
    static_assert(offsetof(steam::SteamStubHeaderV31, Flags) == 0x3Cu);

    steam::SteamStubHeaderV31 header{};
    header.SteamAppId = 489830u; // Skyrim SE: 0x77966.
    header.Flags = 0u;
    header.OriginalEntryPoint = 0x153BC64u;

    // Regression: the old, structurally incomplete header read SteamAppId as
    // Flags. Since 0x77966 contains bit 0x04, it falsely classified Skyrim as
    // NoEncryption and jumped to the original entry point without decrypting /
    // restoring its code section.
    REQUIRE(header.SteamAppId == 0x77966u);
    REQUIRE(steam::HasEncryptedCodeSection(header));
    REQUIRE(header.OriginalEntryPoint == 0x153BC64u);
}

TEST_CASE("SteamStub no-encryption flag alone bypasses code-section decryption", "[launcher][steam-ceg][runtime-compatibility]")
{
    steam::SteamStubHeaderV31 header{};
    header.SteamAppId = 489830u;
    header.Flags = static_cast<uint32_t>(steam::SteamStubDrmFlags::NoEncryption);

    REQUIRE_FALSE(steam::HasEncryptedCodeSection(header));
}

TEST_CASE("SteamStub unrelated DRM flags do not bypass code-section decryption", "[launcher][steam-ceg][runtime-compatibility]")
{
    steam::SteamStubHeaderV31 header{};
    header.SteamAppId = 489830u;
    header.Flags =
        static_cast<uint32_t>(steam::SteamStubDrmFlags::NoModuleVerification) |
        static_cast<uint32_t>(steam::SteamStubDrmFlags::NoOwnershipCheck) |
        static_cast<uint32_t>(steam::SteamStubDrmFlags::NoDebuggerCheck) |
        static_cast<uint32_t>(steam::SteamStubDrmFlags::NoErrorDialog);

    REQUIRE(steam::HasEncryptedCodeSection(header));
}
