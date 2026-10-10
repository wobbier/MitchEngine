#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Maps asset GUIDs to project-relative paths and back, so references survive renames and moves
// (see SceneSerializer::RemapAssetReferences). GUIDs live in .meta files, and inside prefabs
// (top-level "AssetGUID"). Populated by scanning and kept current as metas are saved and the
// editor moves assets.
class AssetDatabase
{
public:
    static AssetDatabase& Get();

    // Scans every .meta, and every prefab's GUID, under the roots (e.g. "Assets", "Engine/Assets").
    void Refresh( const std::vector<std::string>& InRoots );
    // Scans the default roots unless a scan already ran (tools builds scan at startup; game builds
    // only when a referenced asset has gone missing).
    void EnsureScanned();
    bool IsScanned() const;

    void Register( const std::string& InLocalPath, uint64_t InGUID );
    // Forgets an asset, or every asset under a folder.
    void Unregister( const std::string& InLocalPath );

    // An asset or a folder of assets moved. Re-keys them and remembers the old paths, so FindGUID
    // and FindMovedPath still answer for references to the old location.
    void Move( const std::string& InFrom, const std::string& InTo );
    // Where an asset that used to live at InFormerPath is now: empty unless it moved this session.
    std::string FindMovedPath( const std::string& InFormerPath ) const;

    // Empty / 0 when unknown. FindGUID also knows the former paths of moved assets.
    std::string FindPath( uint64_t InGUID ) const;
    uint64_t FindGUID( const std::string& InLocalPath ) const;

    size_t GetAssetCount() const;

private:
    mutable std::mutex m_mutex;
    std::unordered_map<uint64_t, std::string> m_paths;
    std::unordered_map<std::string, uint64_t> m_guids;
    std::unordered_map<std::string, uint64_t> m_formerPaths;
    bool m_scanned = false;
};
