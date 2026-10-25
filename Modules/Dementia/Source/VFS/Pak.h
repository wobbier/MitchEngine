#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "Pointers.h"
#include "VFS/Mount.h"

// .mpak: the engine's package format (VFS-3). One file carrying a logical tree:
//
//   [MpakHeader][entry blobs...][entry table]
//
// The table lives at the end so the writer streams blobs without knowing the count upfront;
// the header is patched last. Paths are forward-slash, mount-relative, case-preserved.
// Entries are stored raw (compression is a later, per-entry flag).
namespace Mpak
{
    struct Header
    {
        static constexpr char kMagic[4] = { 'M', 'P', 'A', 'K' };
        static constexpr uint32_t kVersion = 1;

        char Magic[4] = { 'M', 'P', 'A', 'K' };
        uint32_t Version = kVersion;
        uint64_t TableOffset = 0;
        uint64_t EntryCount = 0;
    };

    struct Entry
    {
        uint64_t Offset = 0;
        uint64_t Size = 0;
    };

    // Streams every (path, bytes) pair the provider yields into InPakPath. The provider calls
    // the sink once per entry; bytes are written in call order.
    using EntrySink = std::function<void( const std::string& InPath, const void* InBytes, uint64_t InSize )>;
    using EntryProvider = std::function<void( const EntrySink& InSink )>;
    bool Write( const std::string& InPakPath, const EntryProvider& InProvider );

    // Packs what the VFS serves under each logical root (directories enumerate recursively,
    // file roots pack directly), entries keyed by their full logical paths — the mounted overlay
    // (game over engine) bakes in at pack time. Dependency-aware packing is the cooker's job
    // (SHIP-2); callers name their roots.
    bool WriteFromVfs( const std::string& InPakPath, const std::vector<std::string>& InLogicalRoots );
}

// A read-only mount over one .mpak. Open parses the table; reads open their own window on the
// pak file, so concurrent loader threads never share stream state.
class PakMount final
    : public IMountSource
{
public:
    // nullptr when the file is missing or not an .mpak this build reads.
    static SharedPtr<PakMount> Open( const std::string& InPakPath );

    VfsStat Stat( const std::string& InRelativePath ) const final;
    std::string GetPhysicalPath( const std::string& InRelativePath ) const final;
    bool ReadText( const std::string& InRelativePath, std::string& OutText ) const final;
    bool ReadBytes( const std::string& InRelativePath, std::vector<uint8_t>& OutBytes ) const final;
    bool WriteText( const std::string& InRelativePath, const std::string& InText ) const final;
    void Enumerate( const std::string& InRelativeDirectory, bool InRecursive,
                    const std::function<void( const std::string& InRelativePath, bool InIsDirectory )>& InVisitor ) const final;
    bool GetStreamWindow( const std::string& InRelativePath, VfsStreamWindow& OutWindow ) const final;

    const std::string& GetPakPath() const { return m_pakPath; }
    size_t GetEntryCount() const { return m_entries.size(); }

private:
    PakMount() = default;

    std::string m_pakPath;
    std::map<std::string, Mpak::Entry> m_entries;
};
