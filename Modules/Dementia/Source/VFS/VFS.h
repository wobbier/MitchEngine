#pragma once
#include <mutex>
#include <string>
#include <vector>

#include "Dementia.h"
#include "Pointers.h"
#include "VFS/Mount.h"

class Path;

// One entry in the mount table.
struct VfsMount
{
    // The logical prefix this mount serves: "" for the overlay roots, "user://" for user state.
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
    // The prefix of the mount that answered ("" or "user://").
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

// The virtual filesystem: logical paths in, bytes or physical locations out.
//
// Defaults reproduce the historical two-root resolver: the game root (the working directory,
// priority 100) over the engine root ("<cwd>/Engine", priority 0). "user://" is the per-user
// writable root: "<cwd>/.tmp/User" until something remounts it (shipped builds point it at the
// OS pref dir in Engine::Init). Mods mount at 200 + load order when they arrive.
class VFS
{
public:
    static constexpr const char* kUserScheme = "user://";

    static VFS& Get();

    // Resolves a normalized logical path (no scheme = relative to the overlay roots).
    // InPrimaryOnly skips the fallback mounts: only the highest-priority match answers
    // (Path's historical Raw flag).
    VfsResolved Resolve( const std::string& InLogicalPath, bool InPrimaryOnly = false ) const;

    void Mount( const std::string& InPrefix, const SharedPtr<IMountSource>& InSource, int InPriority, bool InWritable, const std::string& InDisplayPrefix = {} );
    void MountDirectory( const std::string& InPrefix, const std::string& InDirectory, int InPriority, bool InWritable = true, const std::string& InDisplayPrefix = {} );
    // Drops every mount serving InPrefix.
    void UnmountAll( const std::string& InPrefix );

    // Replaces the user:// mount.
    void SetUserRoot( const std::string& InDirectory );
    std::string GetUserRoot() const;

    // Back to the two default roots + the dev user:// mount (tests).
    void ResetToDefaults();

    // Reads/writes what a Path already resolved to. The one seam loaders go through, so pak
    // sources can slot in without touching call sites again.
    bool ReadText( const Path& InPath, std::string& OutText ) const;
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

private:
    VFS();
    ME_HARDSTUCK( VFS );

    void InstallDefaults();
    void Swap( std::vector<VfsMount> InMounts );

    mutable std::mutex m_mutex;
    SharedPtr<const MountTable> m_table;
};
