#pragma once
#include <atomic>
#include <mutex>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "Dementia.h"
#include "Pointers.h"
#include "VFS/Mount.h"

class Path;

// One entry in the mount table.
struct VfsMount
{
    // The logical prefix this mount serves: "" for the overlay roots, "user://" for user state,
    // or a folder prefix ending in '/' ("Engine/") that serves only the paths spelled under it.
    std::string Prefix;
    SharedPtr<IMountSource> Source;
    int Priority = 0;
    bool Writable = true;
    // Prepended to the mount-relative path for Path::GetLocalPath ("Engine/" on the engine root),
    // so local paths keep round-tripping through serialization the way they always have.
    std::string DisplayPrefix;
};

// An immutable snapshot of the mounts, sorted by priority (highest first; first mounted wins
// ties). Lookups walk a snapshot, so a mount swap never races a loader thread mid-resolve.
struct MountTable
{
    std::vector<VfsMount> Mounts;
};

// What a logical path resolved to.
struct VfsResolved
{
    // Absolute filesystem path: the hit, or where a write would land when nothing exists yet.
    std::string PhysicalPath;
    // DisplayPrefix + the trimmed relative path; what Path exposes as its local path.
    std::string DisplayPath;
    // The prefix of the mount that answered ("", "user://" or a folder prefix like "Engine/").
    std::string MountPrefix;
    // The answering mount's source and the path relative to it, for reads through the mount.
    SharedPtr<IMountSource> Source;
    std::string RelativePath;
    bool Exists = false;
    bool IsFile = false;
    // False when no mount serves the path's prefix (an unknown scheme).
    bool Resolved = false;
    bool Writable = false;
};

// Chunked reads over a resolved Path: for consumers that stream instead of loading whole
// files (FMOD, large caches). Reads a window of a physical file — the whole file for
// directory mounts, the entry's slice for pak mounts — so positions are always entry-relative.
class VfsFileStream
{
public:
    bool Open( const Path& InPath );
    void Close();
    bool IsOpen() const { return m_open; }
    uint64_t Size() const { return m_size; }
    // Returns the bytes actually read (short reads at the end of the window).
    size_t Read( void* OutBuffer, size_t InSize );
    bool Seek( uint64_t InPosition );

private:
    std::ifstream m_stream;
    uint64_t m_begin = 0;
    uint64_t m_cursor = 0;
    uint64_t m_size = 0;
    bool m_open = false;
};

// The virtual filesystem: logical paths in, bytes or physical locations out.
//
// Defaults reproduce the historical two-root resolver: the game root (the working directory,
// priority 100) over the engine root ("<cwd>/Engine", priority 0). "user://" is the per-user
// writable root: "<cwd>/.tmp/User" until something remounts it (shipped builds point it at the
// OS pref dir in Engine::Init). Mods mount at 200 + load order when they arrive.
//
// An engine outside the project (a C#-only project using the editor's engine; see Project) is
// also mounted at the folder prefix "Engine/" above the game root, so "Engine/..." spellings
// keep resolving the way they do when the engine is the project's Engine/ subfolder.
class VFS
{
public:
    static constexpr const char* kUserScheme = "user://";
    static constexpr const char* kEnginePrefix = "Engine/";

    static VFS& Get();

    // Where the default mounts point (absolute directories); the defaults are reinstalled. Until
    // this is called they are the working directory and "<cwd>/Engine".
    void SetDefaultRoots( const std::string& InGameRoot, const std::string& InEngineRoot );
    std::string GetGameRoot() const;
    std::string GetEngineRoot() const;

    // Resolves a normalized logical path (no scheme = relative to the overlay roots).
    // InPrimaryOnly skips the fallback mounts: only the highest-priority match answers
    // (Path's historical Raw flag).
    VfsResolved Resolve( const std::string& InLogicalPath, bool InPrimaryOnly = false ) const;

    void Mount( const std::string& InPrefix, const SharedPtr<IMountSource>& InSource, int InPriority, bool InWritable, const std::string& InDisplayPrefix = {} );
    void MountDirectory( const std::string& InPrefix, const std::string& InDirectory, int InPriority, bool InWritable = true, const std::string& InDisplayPrefix = {} );
    // Mounts an .mpak read-only; false when the file is missing or unreadable.
    bool MountPak( const std::string& InPrefix, const std::string& InPakPath, int InPriority, const std::string& InDisplayPrefix = {} );
    // Drops every mount serving InPrefix.
    void UnmountAll( const std::string& InPrefix );
    // Drops the mounts backed by InSource.
    void Unmount( const SharedPtr<IMountSource>& InSource );

    // Replaces the user:// mount.
    void SetUserRoot( const std::string& InDirectory );
    std::string GetUserRoot() const;

    // Back to the default roots + the dev user:// mount (tests).
    void ResetToDefaults();

    // Reads/writes what a Path already resolved to. The one seam loaders go through, so pak
    // sources can slot in without touching call sites again.
    bool ReadText( const Path& InPath, std::string& OutText ) const;
    bool ReadBytes( const Path& InPath, std::vector<uint8_t>& OutBytes ) const;
    bool WriteText( const Path& InPath, const std::string& InText ) const;

    // Resolve-then-IO conveniences for logical paths.
    bool ReadText( const std::string& InLogicalPath, std::string& OutText ) const;
    bool ReadBytes( const std::string& InLogicalPath, std::vector<uint8_t>& OutBytes ) const;
    // Writes through the highest-priority writable mount serving the path, creating directories.
    bool WriteText( const std::string& InLogicalPath, const std::string& InText ) const;

    // The union of every matching mount's entries, higher priority winning duplicates,
    // visited in sorted order with logical paths.
    void Enumerate( const std::string& InLogicalDirectory, bool InRecursive,
                    const std::function<void( const std::string& InLogicalPath, bool InIsDirectory )>& InVisitor ) const;

    // The current snapshot; resolution never locks while walking it.
    SharedPtr<const MountTable> Snapshot() const;

    // "user://Saves/slot0" -> "user://". Empty when InLogicalPath has no scheme.
    static std::string GetScheme( const std::string& InLogicalPath );

    // Read recording (--record-deps; the cooker ships what a run read). While on, every
    // successful read through a mount — ReadText, ReadBytes, VfsFileStream — notes the logical
    // path it served, in the overlay spelling ("Engine/Assets/X" reads note "Assets/X"). Only
    // the overlay roots are recorded: user:// state and absolute paths aren't content.
    // Thread safe; while off, a read pays one relaxed atomic load.
    void SetRecordingReads( bool InRecording );
    bool IsRecordingReads() const;
    // Every path noted since recording started, sorted.
    std::vector<std::string> GetRecordedReads() const;
    void ClearRecordedReads();
    // For readers that open a resolved location themselves.
    void NoteRead( const VfsResolved& InResolved ) const;
    // The recorded spelling of a resolution; empty for anything that isn't overlay content.
    static std::string RecordedSpelling( const VfsResolved& InResolved );

private:
    VFS();
    ME_HARDSTUCK( VFS );

    void InstallDefaults();
    void Swap( std::vector<VfsMount> InMounts );

    mutable std::mutex m_mutex;
    SharedPtr<const MountTable> m_table;
    std::string m_gameRoot;
    std::string m_engineRoot;

    std::atomic<bool> m_recording{ false };
    mutable std::mutex m_recordMutex;
    mutable std::set<std::string> m_recorded;
};
