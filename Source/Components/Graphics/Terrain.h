#pragma once

#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Path.h"
#include "Terrain/ResidentTerrain.h"
#include "Terrain/TerrainLod.h"

class TerrainResource;
class TerrainCore;
class RenderCore;
namespace TerrainData { class TerrainStreamCache; struct StreamStats; }

class Terrain : public Component<Terrain>
{
    ME_REFLECTABLE( Terrain )
    friend class TerrainCore;
    friend class RenderCore;
public:
    Terrain() : Component( "Terrain" ) {}

    Path Asset;
    Vector3 Color{ 0.3f, 0.48f, 0.2f };
    float Roughness = 0.9f;
    bool CastShadows = true;
    float LodPixelError = 2.f;
    float LodHysteresis = 0.2f;
    int ForcedLod = -1;
    bool ShowLods = false;
    bool ShowPatches = false;
    bool ShowResidency = false;
    bool StreamDetail = true;
    float StreamDistance = 384.f, CollisionDistance = 8.f;
    int CpuBudgetMiB = 256, GpuBudgetMiB = 128, CollisionBudgetMiB = 512;
    int UploadKiB = 1024;
    float UploadMilliseconds = 2.f;
    const TerrainData::StreamStats* GetStreamingStats() const;
    void WaitForStreamingRequest();
    const std::vector<TerrainData::PatchLodChoice>& GetPatchLods() const { return m_patchLods; }
    const std::string& GetLodError() const { return m_lodError; }
    uint64_t GetLodTriangles() const { return m_lodTriangles; }
    uint64_t GetFullDetailTriangles() const { return m_fullTriangles; }
    bool Collision = true;
    float Friction = 0.6f;
    float Restitution = 0.f;

    std::shared_ptr<const TerrainData::ResidentTerrain> GetData() const { return m_valid ? m_data : nullptr; }
    const std::string& GetError() const { return m_error; }
    bool IsLoading() const;
    // Retry/reload without waiting on file I/O. Existing draw/physics snapshots own their lifetimes.
    void ReloadAsset();
    // Procedural/test input: shares immutable prepared data; does not add scene children.
    void SetData( std::shared_ptr<const TerrainData::ResidentTerrain> InData );

#if USING( ME_EDITOR )
    void OnEditorInspect() override;
    // Asset-document previews are immutable, shared across instances, and preserve scene refs.
    static void SetEditorPreview( const Path& InAsset, std::shared_ptr<const TerrainData::ResidentTerrain> InData );
#endif

private:
    void Resolve( bool InWait );
    SharedPtr<TerrainResource> m_resource;
    std::shared_ptr<TerrainData::TerrainStreamCache> m_stream;
    std::shared_ptr<const TerrainData::ResidentTerrain> m_data;
    std::string m_loadedAsset;
    std::string m_error, m_lodError;
    std::vector<TerrainData::PatchLodChoice> m_patchLods;
    uint64_t m_lodTriangles = 0, m_fullTriangles = 0;
    bool m_valid = false;
};
ME_REGISTER_COMPONENT_FOLDER( Terrain, "Rendering" )
