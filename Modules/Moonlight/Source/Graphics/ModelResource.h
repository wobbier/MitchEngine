#pragma once 
#include "Graphics/Texture.h"
#include "Graphics/MeshData.h"
#include "Resource/Resource.h"
#include "Path.h"
#include <memory>
#include <string>
#include <vector>
#include "Scene/Node.h"
#include "Resource/MetaRegistry.h"
#include "Scene/AnimationClip.h"
#include "assimp/material.h"

namespace Assimp
{
    class Importer;
}
struct aiScene;
struct aiNode;
struct aiMaterial;
struct aiMesh;
struct aiNodeAnim;

namespace Moonlight
{
    class MeshData;
    class Material;
}

class ModelResource
    : public Resource
{
    friend class RenderCore;
public:
    ModelResource( const Path& path );
    ~ModelResource();

    virtual bool Load() final;
    // Async (ResourceCache::GetAsync): the cooked file is read and the clips built on a loader
    // thread; meshes, materials and the node tree are made on the main thread (GPU objects).
    bool SupportsAsyncLoad() const final;
    bool LoadAsync() final;
    bool FinishAsyncLoad() final;
    Moonlight::Node RootNode;
    const std::vector<Moonlight::MeshData*>& GetAllMeshes() const;
    const std::vector<Moonlight::AnimationClip>& GetAnimations() const;
private:
    // Read by LoadAsync, consumed by FinishAsyncLoad.
    std::unique_ptr<Assimp::Importer> m_importer;
    const aiScene* m_pendingScene = nullptr;
    std::vector<Moonlight::MeshData*> m_allMeshData;
    std::vector<Moonlight::AnimationClip> m_animations;
    // Levels of detail per aiMesh, built by LoadAsync (import settings), uploaded with the meshes.
    std::vector<std::vector<Moonlight::MeshLod>> m_pendingLods;

    void BuildLods( const aiScene* inScene );

    void ProcessNode( aiNode* node, const aiScene* inScene, Moonlight::Node& inParent, glm::mat4 inParentTransform );
    void ProcessAnimations( const aiScene* inScene );
    // Bone names, offsets and per-vertex weights (max 4) of a skinned mesh.
    void ProcessSkin( aiMesh* mesh, Moonlight::MeshData& outMesh, const std::vector<glm::vec3>& positions );

    Moonlight::MeshData* ProcessMesh( aiMesh* mesh, Moonlight::Node& inParent, const aiScene* scene );

    bool LoadMaterialTextures( SharedPtr<Moonlight::Material> newMaterial, aiMaterial* mat, aiTextureType type, const Moonlight::TextureType& typeName );
};

struct ModelResourceMetadata
    : public MetaBase
{
    ModelResourceMetadata( const Path& filePath ) : MetaBase( filePath )
    {
    }

    void OnSerialize( json& inJson ) override;
    void OnDeserialize( const json& inJson ) override;

    virtual std::string GetExtension2() const override;

    // Import settings, baked into the cooked model (node translations, vertices, bone offsets and
    // animation positions are scaled; transforms keep a scale of 1).
    float ImportScale = 1.f;
    // Convert the file's units to metres (FBX UnitScaleFactor: centimetre assets such as Synty's
    // come in 100x smaller). Files without units are taken as metres.
    bool ConvertUnits = false;
    // The scale the cook applies for a file declaring InUnitScaleFactor (FBX: centimetres per unit).
    float GetBakedScale( double InUnitScaleFactor ) const;

    // Levels of detail, simplified from each mesh when the model loads (not baked into the cook, so
    // changes apply the next time the model loads).
    bool GenerateLODs = false;
    Moonlight::MeshLodSettings LODs;

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() final;
#endif
#if USING( ME_TOOLS )
    void Export() override;
    // The Assimp cook runs off the main thread on hot reload.
    bool ExportsInBackground() const override
    {
        return true;
    }
#endif
};

struct ModelResourceMetadataObj
    : public ModelResourceMetadata
{
    ModelResourceMetadataObj( const Path& filePath ) : ModelResourceMetadata( filePath )
    {
    }
};


ME_REGISTER_METADATA( "fbx", ModelResourceMetadata );
ME_REGISTER_METADATA( "obj", ModelResourceMetadataObj );
