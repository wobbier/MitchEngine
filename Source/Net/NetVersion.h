#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class BitReader;
class BitWriter;

// Bumped whenever the wire format changes.
constexpr uint32_t kNetProtocolVersion = 1;

// What a client and a server must agree on before playing together (NET-2), sent in the
// handshake. A server rejects a client whose version differs and says what differs.
struct NetVersion
{
    uint32_t Protocol = kNetProtocolVersion;
    uint64_t Build = 0;         // the game (product name + version) and the engine's net protocol
    uint64_t ScriptSchema = 0;  // the C# script classes (NET-3 adds their replicated fields)
    uint64_t ModSet = 0;        // net-required mods (MOD-3); 0 = none
    uint64_t Scene = 0;         // the placed entities (static NetIds): the scene's GUIDs, sorted

    void Write( BitWriter& InWriter ) const;
    static NetVersion Read( BitReader& InReader );

    // The names of the fields that differ ("protocol", "build", "scripts", "mods", "scene").
    std::vector<std::string> Differences( const NetVersion& InOther ) const;
    bool operator==( const NetVersion& InOther ) const { return Differences( InOther ).empty(); }

    // FNV-1a 64: the same on every platform (std::hash isn't).
    static uint64_t Hash( std::string_view InText, uint64_t InSeed = 0xCBF29CE484222325ull );
    static uint64_t Hash( uint64_t InValue, uint64_t InSeed );
};
