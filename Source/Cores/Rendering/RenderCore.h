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
    // Releases a compact model's part commands (before its parts are rebuilt or destroyed).
    void ReleaseCompactParts( Entity& InEntity );
    // Drops an entity's foliage draw; the next frame rebuilds it, re-fetching its species models.
    void RefreshFoliage( uint64_t InEntityID ) { RemoveFoliageDraw( InEntityID ); }

    // Levels of detail: the relative screen height of each imported mesh is scaled by this before its level
    // is picked (above 1 keeps detail longer, 0 always draws the coarsest level). --lod-bias sets it.
    float LodBias = 1.f;
    // Seconds a mesh crossfades between LOD levels through a dither (0 = switch at once; ME-279).
    float LodCrossfadeSeconds = 0.25f;

    Cubemap* SkyboxMap = nullptr;
    Moonlight::ShaderCommand* SkyboxShader = nullptr;

    virtual void OnDeviceLost() override;
    virtual void OnDeviceRestored() override;

    virtual void OnStart() override;
    virtual void OnStop() override;

    // ME-323: a second world's RenderCore (a killcam's replay world) shares the renderer's mesh cache
    // with the game world's: it never clears the cache (Init, OnStop), it only pops its own commands.
    void SetSharesRenderer( bool InShares ) { m_sharesRenderer = InShares; }
    // One world is drawn a frame: only the presented world's RenderCore prepares one. One taken off
    // screen empties its mesh commands once (out of every view and shadow) and skips its Update until
    // it's presented again, so the renderer's lights, decals, sprites and wind are the presented world's.
    void SetPresented( bool InPresented ) { m_presented = InPresented; }
    bool IsPresented() const { return m_presented; }

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() final;
#endif

private:
    struct FoliageDraw
    {
        struct Prototype { Moonlight::MeshData* Geometry; Matrix4 Local; };
        struct Source { std::string Path; SharedPtr<ModelResource> Resource; std::vector<Prototype> Parts; std::string Error; bool Prepared = false; };
        // Frame: the renderer frame it was last drawn for (a hidden one's commands are emptied once).
        struct Instance { unsigned Species; std::vector<std::unique_ptr<Mesh>> Parts; uint64_t Frame = 0; };
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
    // Retained mesh commands (ME-390): per mesh cache id, the entity whose own Mesh last wrote a
    // command that can be kept across frames (0: none). Such a command is checked against its
    // sources each frame (RetainedCommandHolds) and rebuilt only when one changed. Reset when the
    // renderer's mesh cache generation changes.
    std::vector<uint64_t> m_retainedOwner;
    uint32_t m_retainedGeneration = UINT32_MAX;
    void ForgetRetained( unsigned int InId ) { if( InId < m_retainedOwner.size() ) m_retainedOwner[InId] = 0; }
    // Per mesh cache id beside m_retainedOwner (ME-399): the material revision the kept command was
    // last checked against, and the material's CastsShadows() then. While no material changed
    // (Moonlight::MaterialRevision), a kept command skips reading its material at all.
    struct RetainedMaterial
    {
        uint32_t Revision = 0;
        bool CastsShadows = false;
    };
    std::vector<RetainedMaterial> m_retainedMaterial;
    uint64_t m_materialChanges = UINT64_MAX;    // MaterialRevision counts at the last gather
    uint64_t m_materialExternalChanges = UINT64_MAX;
    // Counts Updates that gathered (foliage notes which instances it drew in which one).
    uint64_t m_gatherCount = 0;
    bool EnableDebugDraw = false;
    bool m_sharesRenderer = false;
    bool m_presented = true;
    bool m_hidden = false;      // its commands are emptied (not presented)
    void HideCommands();
    // The world is playing (editor: levels of detail then follow the game camera).
    bool m_playing = false;
        //Moonlight::Renderer* m_renderer;
    // OccluderFade (ME-279): this frame's fade target per entity id, and the cameras' update.
    std::unordered_map<uint64_t, float> m_occluderTargets;
    float m_occluderFadeRate = 5.f;
    void UpdateOccluderFades();
};
