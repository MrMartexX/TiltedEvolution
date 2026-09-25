#include <Structs/Skyrim/PartyQuestNativeLoadBridgeRuntimeFingerprint.h>

#include <catch2/catch.hpp>

namespace
{
constexpr PartyQuestSkyrimRuntimeVersion kRuntime{1u, 6u, 1170u, 0u};

constexpr PartyQuestSkyrimExecutableIdentity MakeIdentity(
    uint8_t aFirst,
    uint8_t aLast) noexcept
{
    PartyQuestSkyrimExecutableIdentity identity{};
    identity.Sha256[0] = aFirst;
    identity.Sha256[31] = aLast;
    return identity;
}
}

TEST_CASE(
    "Native load runtime fingerprint is deterministic and contract locked",
    "[quest.party-state][native-load-runtime-fingerprint]")
{
    constexpr auto identity = MakeIdentity(0x42u, 0xE7u);
    constexpr uint64_t fingerprint =
        PartyQuestNativeLoadBridgeRuntimeFingerprintPolicy::Derive(
            kRuntime,
            identity);

    STATIC_REQUIRE(fingerprint == 0x0FEF60E2BB56775Full);
    STATIC_REQUIRE(fingerprint != 0u);
    REQUIRE(
        PartyQuestNativeLoadBridgeRuntimeFingerprintPolicy::Derive(
            kRuntime,
            identity) == fingerprint);
}

TEST_CASE(
    "Native load runtime fingerprint covers every executable digest byte",
    "[quest.party-state][native-load-runtime-fingerprint][identity]")
{
    auto baseline = MakeIdentity(0x42u, 0xE7u);
    const uint64_t expected =
        PartyQuestNativeLoadBridgeRuntimeFingerprintPolicy::Derive(
            kRuntime,
            baseline);
    REQUIRE(expected != 0u);

    for (size_t index = 0u; index < baseline.Sha256.size(); ++index)
    {
        auto changed = baseline;
        changed.Sha256[index] ^= 0x01u;
        REQUIRE(
            PartyQuestNativeLoadBridgeRuntimeFingerprintPolicy::Derive(
                kRuntime,
                changed) != expected);
    }
}

TEST_CASE(
    "Native load runtime fingerprint rejects unsupported or absent runtime identity",
    "[quest.party-state][native-load-runtime-fingerprint][fail-closed]")
{
    PartyQuestSkyrimExecutableIdentity empty{};
    REQUIRE(
        PartyQuestNativeLoadBridgeRuntimeFingerprintPolicy::Derive(
            kRuntime,
            empty) == 0u);

    const auto identity = MakeIdentity(0x42u, 0xE7u);
    for (const auto runtime : {
             PartyQuestSkyrimRuntimeVersion{1u, 6u, 640u, 0u},
             PartyQuestSkyrimRuntimeVersion{1u, 6u, 1170u, 1u},
             PartyQuestSkyrimRuntimeVersion{0u, 0u, 0u, 0u}})
    {
        REQUIRE(
            PartyQuestNativeLoadBridgeRuntimeFingerprintPolicy::Derive(
                runtime,
                identity) == 0u);
    }
}

TEST_CASE(
    "Native load runtime fingerprint changes with bridge contract domain",
    "[quest.party-state][native-load-runtime-fingerprint][contract]")
{
    STATIC_REQUIRE(
        PartyQuestNativeLoadBridgeRuntimeFingerprintPolicy::
            kAddressLibraryAeLoadImplId == 35728u);
    STATIC_REQUIRE(kPartyQuestNativeLoadBridgeFingerprint != 0u);
    STATIC_REQUIRE(
        kPartyQuestRequiredNativeLoadBridgeCapabilities != 0u);

    constexpr auto identity = MakeIdentity(0x42u, 0xE7u);
    constexpr uint64_t fingerprint =
        PartyQuestNativeLoadBridgeRuntimeFingerprintPolicy::Derive(
            kRuntime,
            identity);
    STATIC_REQUIRE(fingerprint != kPartyQuestNativeLoadBridgeFingerprint);
}
