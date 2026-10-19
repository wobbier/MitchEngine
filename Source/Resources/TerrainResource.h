#pragma once

#include "Resource/Resource.h"
#include "Terrain/ResidentTerrain.h"
namespace TerrainData { struct StreamArchive; }

// Background loads perform file I/O, page decoding and CPU preparation on a resource loader thread.
// FinishAsyncLoad publishes one immutable snapshot; rendering and physics retain their own refs.
class TerrainResource : public Resource
{
public:
    explicit TerrainResource( const Path& InPath ) : Resource( InPath ) {}
    bool SupportsAsyncLoad() const override { return true; }
    bool LoadAsync() override;
    bool FinishAsyncLoad() override;
    bool Load() override { return LoadAsync() && FinishAsyncLoad(); }
    void Reload() override { Load(); }

    std::shared_ptr<const TerrainData::ResidentTerrain> GetData() const { return m_data; }
    std::shared_ptr<const TerrainData::StreamArchive> GetArchive() const { return m_archive; }
    // Inspect only after loading completes; the loader thread writes the error.
    const std::string& GetError() const { return m_error; }

private:
    std::shared_ptr<const TerrainData::ResidentTerrain> m_data;
    std::shared_ptr<const TerrainData::ResidentTerrain> m_pending;
    std::string m_error;
    std::shared_ptr<const TerrainData::StreamArchive> m_archive, m_pendingArchive;
};
