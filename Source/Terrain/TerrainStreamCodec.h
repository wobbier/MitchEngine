#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace TerrainData
{
    constexpr uint64_t MaxStreamBlockBytes = 64ull << 20;
    // Version 5 block envelope: little-endian codec and decoded byte count, then raw bytes
    // or bounded runs over 4/8/16-byte planes (codec 1/2/3). Codec 0 stores raw bytes.
    // Inputs are complete 32-bit words.
    // The caller supplies the expected size from validated terrain settings, never the block.
    // Failure preserves the destination; stored blocks are at most raw size + eight bytes.
    bool EncodeStreamBlock( std::span<const uint8_t> InBytes, std::vector<uint8_t>& OutBytes, std::string& OutError );
    bool DecodeStreamBlock( std::span<const uint8_t> InBytes, uint64_t InExpectedBytes, std::vector<uint8_t>& OutBytes, std::string& OutError );
}
