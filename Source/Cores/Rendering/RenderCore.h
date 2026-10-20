#pragma once
#include "ECS/Core.h"
#include "Graphics/Cubemap.h"
#include "Graphics/ShaderCommand.h"
#include "Device/IDevice.h"
#include "Graphics/ModelResource.h"
#include "Jobs/JobSystem.h"
#include <unordered_map>
#include "Terrain/TerrainLod.h"
namespace Moonlight { class MeshIndexData; }

class Mesh;
class Terrain;
class TerrainFoliage;

class RenderCore final
    : public Core<RenderCore>
    , public Moonlight::IDeviceNotify
{
public:
    RenderCore();
    ~RenderCore();

    // Separate init from construction code.
    virtual void Init() final;

    // Each core must update each loop
    virtual void Update( const UpdateContext& inUpdateContext ) final;

    virtual void OnEntityAdded( Entity& NewEntity ) final;
    virtual void OnEntityRemoved( Entity& InEntity ) final;
    void OnRemovedFromWorld() override;

    // Levels of detail: the relative screen height of each imported mesh is scaled by this before its level
    // is picked (above 1 keeps detail longer, 0 always draws the coarsest level). --lod-bias sets it.
    float LodBias = 1.f;

    Cubemap* SkyboxMap = nullptr;
    Moonlight::ShaderCommand* SkyboxShader = nullptr;

    virtual void OnDeviceLost() override;
    virtual void OnDeviceRestored() override;

    virtual void OnStart() override;
    virtual void OnStop() override;

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() final;
#endif

private:
    struct FoliageDraw
    {
        struct Prototype { Moonlight::MeshData* Geometry; Matrix4 Local; };
        struct Source { std::string Path; SharedPtr<ModelResource> Resource; std::vector<Prototype> Parts; std::string Error; bool Prepared = false; };
        struct Instance { unsigned Species; std::vector<std::unique_ptr<Mesh>> Parts; };
        std::vector<Source> Sources;
        std::map<uint64_t, Instance> Instances;
    };
    std::unordered_map<uint64_t, FoliageDraw> m_foliageDraws;
    void RemoveFoliageDraw( uint64_t InID );
    void ClearFoliageDraws();
    void PrepareFoliageDraw( Entity& InEntity, TerrainFoliage& InFoliage, std::span<const TerrainData::LodView> InViews,
        const std::function<void( Mesh*, const Matrix4& )>& InSubmit );
    struct TerrainDraw;
    TerrainDraw* PrepareTerrainDraw( Entity& InEntity, Terrain& InTerrain, std::span<const TerrainData::LodView> InViews );
    void RemoveTerrainDraw( uint64_t InEntityID );
    void RetireTerrainDraw( std::unique_ptr<TerrainDraw> InDraw, const TerrainDraw* InKeep = nullptr );
    void ClearTerrainDraws();
    std::unordered_map<uint64_t, std::unique_ptr<TerrainDraw>> m_terrainDraws;
    struct RetiringTerrain
    {
        std::vector<std::shared_ptr<const TerrainData::ResidentPatch>> Patches;
        uint8_t Frames = 4; // Includes a possible removal before this frame's render preparation.
    };
    std::vector<RetiringTerrain> m_retiringTerrains;
    std::unordered_map<uint64_t, std::shared_ptr<Moonlight::MeshIndexData>> m_terrainTopology;
    std::unordered_map<uint64_t, std::vector<unsigned int>> m_compactCommands;
    std::unordered_map<uint64_t, unsigned int> m_meshCommands;
    bool EnableDebugDraw = false;
    // The world is playing (editor: levels of detail then follow the game camera).
    bool m_playing = false;
        //Moonlight::Renderer* m_renderer;
};
