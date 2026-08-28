#include <steam/SteamCrypto.h>

#include <catch2/catch.hpp>

TEST_CASE("SteamStub no-encryption flag preserves the current Skyrim code section", "[launcher][steam-ceg][runtime-compatibility]")
{
    steam::SteamStubHeaderV31 header{};
    header.Flags = 0x77966;
    header.OriginalEntryPoint = 0x15A8514;

    REQUIRE_FALSE(steam::HasEncryptedCodeSection(header));
    REQUIRE(header.OriginalEntryPoint == 0x15A8514);
}

TEST_CASE("SteamStub encrypted profiles still require code-section decryption", "[launcher][steam-ceg][runtime-compatibility]")
{
    steam::SteamStubHeaderV31 header{};
    header.Flags = 0;

    REQUIRE(steam::HasEncryptedCodeSection(header));
}
