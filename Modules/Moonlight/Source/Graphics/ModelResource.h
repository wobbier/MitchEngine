#pragma once 
#include "Graphics/Texture.h"
#include "Graphics/MeshData.h"
#include "Resource/Resource.h"
#include "Path.h"
#include <string>
#include <vector>
#include "Scene/Node.h"
#include "Resource/MetaRegistry.h"
#include "Scene/AnimationClip.h"
#include "assimp/material.h"
#include "Scene/Keyframe.h"
#include "Scene/Skeleton.h"

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
    Moonlight::Node RootNode;
    const std::vector<Moonlight::MeshData*>& GetAllMeshes() const;
    const std::vector<Moonlight::AnimationClip>& GetAnimations() const;
private:
    std::vector<Moonlight::MeshData*> m_allMeshData;
    std::vector<Moonlight::AnimationClip> m_animations;
    Moonlight::Skeleton m_skeleton;

    void ProcessNode( aiNode* node, const aiScene* inScene, Moonlight::Node& inParent, glm::mat4 inParentTransform );
    void ProcessSkeleton( const aiScene* inScene );
    void ProcessAnimations( const aiScene* inScene );
    std::vector<Moonlight::Keyframe> BuildKeyframes( const aiNodeAnim* channel, float ticksPerSecond );

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

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() final;
#endif
#if USING( ME_TOOLS )
    void Export() override;
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
