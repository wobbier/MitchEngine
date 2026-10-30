#pragma once
#include "Graphics/MeshLod.h"
#include "ECS/ComponentDetail.h"
#include "ECS/Component.h"
#include "RenderCommands.h"
#include "Dementia.h"
#include "Graphics/MeshData.h"
#include "Graphics/Texture.h"
#include "Graphics/Material.h"
#include "Graphics/ShaderCommand.h"
#include "Graphics/MaterialDetail.h"
#include "Pointers.h"
#include <limits>
class MaterialTest
{
public:
    std::map<std::string, MaterialTest> Folders;
    std::map<std::string, MaterialInfo*> Reg;
};
class Mesh
    : public Component<Mesh>
{
    friend class RenderCore;
    friend class EditorCore;
    friend class Model;
public:
    Mesh();
    Mesh( Moonlight::MeshType InType, Moonlight::Material* InMaterial );
    Mesh( Moonlight::MeshData* mesh );
    ~Mesh();

    // Separate init from construction code.
    virtual void Init() final;

    unsigned int GetId();

    Moonlight::MeshData* MeshReferece = nullptr;
    SharedPtr<Moonlight::Material> MeshMaterial;

    Moonlight::MeshType GetType() const;
    void BindModelMesh( Moonlight::MeshData* InMesh );

    // Renders into shadow maps (directional cascades and spot lights).
    bool CastShadows = true;
    // Screen-door fade (ME-279), at runtime only (not saved): 0 = solid, up to 1 = gone, dithered
    // so it needs no sorting. OccluderFade drives it for whatever blocks a camera's view.
    float DitherFade = 0.f;
    // Runtime portal slicing. Zero plane disables clipping; proxies override only their draw matrix.
    glm::vec4 PortalClipPlane = glm::vec4( 0.f );
    bool PortalImage = false;
    Matrix4 PortalImageWorld;


    // Skinned meshes: the bone node entities (found by name below the owning Model) and this
    // frame's palette of mesh-space bone matrices.
    bool IsSkinned() const;
    bool HasVertexAnimation() const { return MeshReferece && MeshReferece->VertexCache; }
    // World-space bounds: the last skinned pose for skinned meshes, else the mesh bounds placed by InWorld.
    AABB GetWorldBounds( const Matrix4& InWorld ) const;

    virtual void OnSerialize( json& outJson ) final;
    virtual void OnDeserialize( const json& inJson ) final;
private:
    unsigned int Id = std::numeric_limits<unsigned int>::max();
    std::vector<EntityHandle> m_bones;
    bool m_bonesResolved = false;
    std::vector<glm::mat4> m_skinPalette;
    // LOD crossfade (ME-279): the level shown and the one fading out.
    Moonlight::LodCrossfade m_lodCrossfade;
    AABB m_skinBounds;
    // Finds the bone entities; returns false while any is missing.
    bool ResolveBones();
    // Fills the palette from the bones' world matrices; returns the skinned world bounds.
    AABB UpdateSkin( const glm::mat4& InMeshWorld );
    // Allocation/uploads run on the render API thread; sampling can run in mesh jobs.
    void PrepareVertexAnimation();
    bool BeginVertexAnimation();
    AABB SampleVertexAnimation( size_t InBegin, size_t InEnd );
    void EndVertexAnimation( const AABB& InBounds );
    void UploadVertexAnimation();
    bgfx::DynamicVertexBufferHandle m_posedVertices = BGFX_INVALID_HANDLE;
    Moonlight::MeshData* m_vertexAnimationSource = nullptr;
    std::vector<Moonlight::PosNormTexTanBiVertex> m_vertexAnimationVertices;
    float m_vertexAnimationTime = -1.f;
    float m_nextVertexAnimationTime = 0.f;
    bool m_vertexAnimationUpload = false;
    AABB m_vertexAnimationBounds;
    Moonlight::MeshType Type;
    Path MaterialAssetPath;
    json m_importedMaterialDefaults;
    json m_pendingMaterial;

    std::string GetMeshTypeString( Moonlight::MeshType InType );
    Moonlight::MeshType GetMeshTypeFromString( const std::string& InType );

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() final;

    void DoMaterialRecursive( const MaterialTest& currentFolder );
    void SelectMaterial( const std::pair<std::string, MaterialInfo*>& ptr, MaterialRegistry& reg );

#endif
};

ME_REGISTER_COMPONENT_FOLDER( Mesh, "Rendering" )
