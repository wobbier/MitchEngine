#pragma once
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

    // Renders into shadow maps (directional cascades and spot lights).
    bool CastShadows = true;

    // Skinned meshes: the bone node entities (found by name below the owning Model) and this
    // frame's palette of mesh-space bone matrices.
    bool IsSkinned() const;
    // World-space bounds: the last skinned pose for skinned meshes, else the mesh bounds placed by InWorld.
    AABB GetWorldBounds( const Matrix4& InWorld ) const;

    virtual void OnSerialize( json& outJson ) final;
    virtual void OnDeserialize( const json& inJson ) final;
private:
    unsigned int Id = 0;
    std::vector<EntityHandle> m_bones;
    bool m_bonesResolved = false;
    std::vector<glm::mat4> m_skinPalette;
    AABB m_skinBounds;
    // Finds the bone entities; returns false while any is missing.
    bool ResolveBones();
    // Fills the palette from the bones' world matrices; returns the skinned world bounds.
    AABB UpdateSkin( const glm::mat4& InMeshWorld );
    Moonlight::MeshType Type;
    Path MaterialAssetPath;

    std::string GetMeshTypeString( Moonlight::MeshType InType );
    Moonlight::MeshType GetMeshTypeFromString( const std::string& InType );

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() final;

    void DoMaterialRecursive( const MaterialTest& currentFolder );
    void SelectMaterial( const std::pair<std::string, MaterialInfo*>& ptr, MaterialRegistry& reg );

#endif
};

ME_REGISTER_COMPONENT_FOLDER( Mesh, "Rendering" )