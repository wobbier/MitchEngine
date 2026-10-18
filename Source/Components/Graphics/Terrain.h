#pragma once

#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Path.h"
#include "Terrain/ResidentTerrain.h"

class TerrainResource;
class TerrainCore;

class Terrain : public Component<Terrain>
{
    ME_REFLECTABLE( Terrain )
    friend class TerrainCore;
public:
    Terrain() : Component( "Terrain" ) {}

    Path Asset;
    Vector3 Color{ 0.3f, 0.48f, 0.2f };
    float Roughness = 0.9f;
    bool CastShadows = true;
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
#endif

private:
    void Resolve( bool InWait );
    SharedPtr<TerrainResource> m_resource;
    std::shared_ptr<const TerrainData::ResidentTerrain> m_data;
    std::string m_loadedAsset;
    std::string m_error;
    bool m_valid = false;
};
ME_REGISTER_COMPONENT_FOLDER( Terrain, "Rendering" )
