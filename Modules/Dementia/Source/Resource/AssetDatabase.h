#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Maps asset GUIDs (stored in .meta files) to project-relative paths and back, so references can
// survive renames/moves. Populated by scanning .meta files and kept current as metas are saved.
class AssetDatabase
{
public:
    static AssetDatabase& Get();

    // Scans every .meta under the roots (e.g. "Assets", "Engine/Assets").
    void Refresh( const std::vector<std::string>& InRoots );

    void Register( const std::string& InLocalPath, uint64_t InGUID );
    void Unregister( const std::string& InLocalPath );

    // Empty / 0 when unknown.
    std::string FindPath( uint64_t InGUID ) const;
    uint64_t FindGUID( const std::string& InLocalPath ) const;

    size_t GetAssetCount() const;

private:
    mutable std::mutex m_mutex;
    std::unordered_map<uint64_t, std::string> m_paths;
    std::unordered_map<std::string, uint64_t> m_guids;
};
