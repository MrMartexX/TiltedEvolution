#pragma once

#include <Structs/Skyrim/PartyQuestNativeLoadBridge.h>
#include <Structs/Skyrim/PartyQuestSkyrimPapyrusRuntimeProfileResolver.h>

#include <cstddef>
#include <cstdint>

/**
 * Deterministic correlation fingerprint for the exact native LoadGame runtime
 * contract.
 *
 * This 64-bit value is not a trust primitive and never replaces the complete
 * executable SHA-256 stored in PartyQuestSkyrimExecutableIdentity. Its only
 * purpose is to give the bridge descriptor/call owner a compact, deterministic
 * correlation value derived from already-verified runtime evidence.
 *
 * Input domain, byte order and contract fields are fixed by v1:
 *   domain ASCII bytes, no NUL
 *   runtime tuple as four little-endian uint32 values
 *   all 32 executable SHA-256 bytes in digest order
 *   Address Library AE id 35728 as little-endian uint64
 *   bridge fingerprint and required capability mask as little-endian uint64
 *   descriptor ABI, payload ABI and implementation version as little-endian
 *   uint32 values
 *
 * FNV-1a/64 is used only as deterministic compaction. Collision resistance is
 * not relied upon: every trusted resolver must still compare the complete
 * executable identity independently.
 */
struct PartyQuestNativeLoadBridgeRuntimeFingerprintPolicy final
{
    inline static constexpr uint64_t kAddressLibraryAeLoadImplId = 35728u;
    inline static constexpr uint64_t kFnvOffsetBasis =
        14695981039346656037ull;
    inline static constexpr uint64_t kFnvPrime = 1099511628211ull;
    inline static constexpr char kDomain[] =
        "PartyQuest.NativeLoad.RuntimeFingerprint.V1";

    [[nodiscard]] static constexpr uint64_t Derive(
        const PartyQuestSkyrimRuntimeVersion& acRuntime,
        const PartyQuestSkyrimExecutableIdentity& acExecutable) noexcept
    {
        if (acRuntime.Major != 1u ||
            acRuntime.Minor != 6u ||
            acRuntime.Patch != 1170u ||
            acRuntime.Build != 0u)
        {
            return 0u;
        }

        bool executableIdentityPresent = false;
        for (const uint8_t value : acExecutable.Sha256)
            executableIdentityPresent = executableIdentityPresent || value != 0u;
        if (!executableIdentityPresent)
            return 0u;

        uint64_t hash = kFnvOffsetBasis;
        for (size_t index = 0u; index + 1u < sizeof(kDomain); ++index)
            AppendByte(hash, static_cast<uint8_t>(kDomain[index]));

        AppendU32(hash, acRuntime.Major);
        AppendU32(hash, acRuntime.Minor);
        AppendU32(hash, acRuntime.Patch);
        AppendU32(hash, acRuntime.Build);

        for (const uint8_t value : acExecutable.Sha256)
            AppendByte(hash, value);

        AppendU64(hash, kAddressLibraryAeLoadImplId);
        AppendU64(hash, kPartyQuestNativeLoadBridgeFingerprint);
        AppendU64(hash, kPartyQuestRequiredNativeLoadBridgeCapabilities);
        AppendU32(hash, kPartyQuestNativeLoadBridgeDescriptorAbi);
        AppendU32(hash, kPartyQuestNativeLoadBridgePayloadAbi);
        AppendU32(hash, kPartyQuestNativeLoadBridgeImplementationVersion);

        // Zero is the ABI sentinel for "no trusted fingerprint". FNV-1a can in
        // principle produce zero, so map only that singular result to 1. The
        // complete SHA-256 remains the actual identity proof.
        return hash != 0u ? hash : 1u;
    }

private:
    static constexpr void AppendByte(
        uint64_t& aHash,
        uint8_t aValue) noexcept
    {
        aHash ^= static_cast<uint64_t>(aValue);
        aHash *= kFnvPrime;
    }

    static constexpr void AppendU32(
        uint64_t& aHash,
        uint32_t aValue) noexcept
    {
        for (uint32_t shift = 0u; shift < 32u; shift += 8u)
        {
            AppendByte(
                aHash,
                static_cast<uint8_t>((aValue >> shift) & 0xFFu));
        }
    }

    static constexpr void AppendU64(
        uint64_t& aHash,
        uint64_t aValue) noexcept
    {
        for (uint32_t shift = 0u; shift < 64u; shift += 8u)
        {
            AppendByte(
                aHash,
                static_cast<uint8_t>((aValue >> shift) & 0xFFu));
        }
    }
};
