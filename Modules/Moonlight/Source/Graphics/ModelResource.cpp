#include "ModelResource.h"

#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <unordered_map>

#include "CLog.h"
#include "Resource/ResourceCache.h"
#include "Graphics/Texture.h"
#include "Graphics/Material.h"
#include "Graphics/MeshData.h"
#include "Scene/Node.h"
#include <stack>
#include "assimp/material.h"
#include "Materials/DiffuseMaterial.h"
#include "Core/Assert.h"

DISABLE_OPTIMIZATION;

void DecomposeMatrix(
    const glm::mat4& inMatrix,
    glm::vec3& outTranslation,
    glm::quat& outRotation,
    glm::vec3& outScale,
    bool& outWasFlipped )
{
    outTranslation = glm::vec3( inMatrix[3] );

    glm::vec3 col0 = glm::vec3( inMatrix[0] );
    glm::vec3 col1 = glm::vec3( inMatrix[1] );
    glm::vec3 col2 = glm::vec3( inMatrix[2] );

    outScale.x = glm::length( col0 );
    outScale.y = glm::length( col1 );
    outScale.z = glm::length( col2 );

    if( outScale.x != 0.0f ) col0 /= outScale.x;
    if( outScale.y != 0.0f ) col1 /= outScale.y;
    if( outScale.z != 0.0f ) col2 /= outScale.z;

    float det = dot( col0, cross( col1, col2 ) );

    // If negative, there's an overall flip => choose one axis to be negative
    if( det < 0.0f )
    {
        float absX = fabs( outScale.x );
        float absY = fabs( outScale.y );
        float absZ = fabs( outScale.z );

        // Flip whichever axis has the largest magnitude
        if( absX >= absY && absX >= absZ )
        {
            outScale.x = -outScale.x;
            col0 = -col0;
        }
        else if( absY >= absX && absY >= absZ )
        {
            outScale.y = -outScale.y;
            col1 = -col1;
        }
        else
        {
            outScale.z = -outScale.z;
            col2 = -col2;
        }
        outWasFlipped = true;
    }

    glm::mat3 rotationMatrix( col0, col1, col2 );
    outRotation = glm::quat_cast( rotationMatrix );
}

glm::mat4 AssimpToGLM( const aiMatrix4x4& from )
{
    glm::mat4 to;

    to[0][0] = from.a1; to[1][0] = from.a2; to[2][0] = from.a3; to[3][0] = from.a4;
    to[0][1] = from.b1; to[1][1] = from.b2; to[2][1] = from.b3; to[3][1] = from.b4;
    to[0][2] = from.c1; to[1][2] = from.c2; to[2][2] = from.c3; to[3][2] = from.c4;
    to[0][3] = from.d1; to[1][3] = from.d2; to[2][3] = from.d3; to[3][3] = from.d4;

    return to;
}

void ScaleNode( aiNode* node, float scale )
{
    aiMatrix4x4 scalingMatrix;
    aiMatrix4x4::Scaling( aiVector3D( scale, scale, scale ), scalingMatrix );

    // Apply the scaling to the node's transformation
    node->mTransformation = scalingMatrix * node->mTransformation;

    // Recursively scale child nodes
    for( unsigned int i = 0; i < node->mNumChildren; ++i )
    {
        ScaleNode( node->mChildren[i], scale );
    }
}

ModelResource::ModelResource( const Path& path )
    : Resource( path )
{
}

ModelResource::~ModelResource()
{
}

bool ModelResource::Load()
{
    Assimp::Importer importer;
    const aiScene* scene;
    // Force model loading 
    if( false )
    {
        Path newPath = Path( FilePath.FullPath );
        ME_ASSERT_MSG( newPath.Exists, "Exported Model Doesn't Exist" );

        scene = importer.ReadFile( FilePath.FullPath.c_str(), aiProcess_Triangulate | aiProcess_CalcTangentSpace | aiProcess_GlobalScale | aiProcess_ConvertToLeftHanded );
        if( !scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode )
        {
            std::cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
            return false;
        }

    }
    else
    {
        Path newPath = Path( FilePath.FullPath + ".assbin" );
        ME_ASSERT_MSG( newPath.Exists, "Exported Model Doesn't Exist" );

        scene = importer.ReadFile( newPath.FullPath.c_str(), 0 );
        if( !scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode )
        {
            std::cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
            return false;
        }
    }

    RootNode.MaterialCache.resize( scene->mNumMaterials );
    m_allMeshData.resize( scene->mNumMeshes );

    RootNode.Name = std::string( scene->mRootNode->mName.C_Str() );
    ProcessNode( scene->mRootNode, scene, RootNode, AssimpToGLM( scene->mRootNode->mTransformation ) );
    importer.FreeScene();

    return true;
}

std::vector<Moonlight::MeshData*> ModelResource::GetAllMeshes()
{
    std::vector<Moonlight::MeshData*> meshes;
    std::stack<Moonlight::Node*> nodes;
    nodes.push( &RootNode );
    while( !nodes.empty() )
    {
        Moonlight::Node& child = *nodes.top();
        nodes.pop();
        for( Moonlight::MeshData* childMesh : child.Meshes )
        {
            meshes.push_back( childMesh );
        }
        for( Moonlight::Node& childNode : child.Nodes )
        {
            nodes.push( &childNode );
        }
    }
    return meshes;
}

void ModelResource::ProcessNode( aiNode* node, const aiScene* scene, Moonlight::Node& parent, glm::mat4 parentTransform )
{
    glm::mat4 nodeTransform = AssimpToGLM( node->mTransformation ); // Convert Assimp transform to GLM
    glm::mat4 worldTransform = /*parentTransform **/ nodeTransform;   // Combine with parent transform

    std::string nodeName = std::string( node->mName.C_Str() );
    glm::vec3 translation;
    glm::vec3 scale;
    glm::quat rotation;
    bool wasFlipped = false;

    DecomposeMatrix( worldTransform, translation, rotation, scale, wasFlipped );

    glm::vec3 position = glm::vec3( worldTransform[3] );
    parent.Name = nodeName;
    parent.Position = Vector3( translation );
    parent.Scale = Vector3( scale );
    parent.NodeMatrix = Matrix4( worldTransform );
    parent.Rotation = Quaternion( rotation );
    parent.IsFlipped = wasFlipped;
    if( nodeName == "SM_Generic_Mountains_Grass_10" )
    {
        Matrix4 testNode = Matrix4( worldTransform );
        printf( "Translation: (%.6f, %.6f, %.6f)\n", translation.x, translation.y, translation.z );
        printf( "Scale:       (%.6f, %.6f, %.6f)\n", scale.x, scale.y, scale.z );
        printf( "Rotation (quat): (%.6f, %.6f, %.6f, %.6f)\n",
            rotation.w, rotation.x, rotation.y, rotation.z );

        glm::vec3 eulerDegrees = glm::degrees( glm::eulerAngles( rotation ) );
        printf( "Rotation (Euler XYZ): (%.2f, %.2f, %.2f)\n",
            eulerDegrees.x, eulerDegrees.y, eulerDegrees.z );
    }

    for( unsigned int i = 0; i < node->mNumMeshes; i++ )
    {
        if( !m_allMeshData[node->mMeshes[i]] )
        {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            m_allMeshData[node->mMeshes[i]] = ProcessMesh( mesh, parent, scene );
            parent.Meshes.push_back( m_allMeshData[node->mMeshes[i]] );
            continue;
        }
        parent.Meshes.push_back( m_allMeshData[node->mMeshes[i]] );
    }

    parent.Nodes.reserve( node->mNumChildren );

    for( unsigned int i = 0; i < node->mNumChildren; i++ )
    {
        // Apply these transforms? I upgraded from like blender 2.8 to 4.3 so maybe this doesn't matter anymore?
        //if( nodeName.rfind( "$AssimpFbx$" ) == std::string::npos ) //_RotationPivotInverse
        {
            parent.Nodes.push_back( Moonlight::Node() );
            ProcessNode( node->mChildren[i], scene, parent.Nodes.back(), worldTransform );
        }
        //else
        //{
        //    ProcessNode( node->mChildren[i], scene, parent, worldTransform );
        //}
    }
}

Moonlight::MeshData* ModelResource::ProcessMesh( aiMesh* mesh, Moonlight::Node& inParent, const aiScene* scene )
{
    std::vector<Moonlight::PosNormTexTanBiVertex> vertices;
    std::vector<uint16_t> indices;

    for( unsigned int i = 0; i < mesh->mNumVertices; i++ )
    {
        Moonlight::PosNormTexTanBiVertex vertex;
        Vector3 vector;

        vertex.Position = { mesh->mVertices[i].x,  mesh->mVertices[i].y,  mesh->mVertices[i].z };

        vector.x = mesh->mNormals[i].x;
        vector.y = mesh->mNormals[i].y;
        vector.z = mesh->mNormals[i].z;
        vertex.Normal = vector;

        if( mesh->mTextureCoords[0] )
        {
            Vector2 vec;

            // A vertex can contain up to 8 different texture coordinates. We assume that we won't use models where a vertex can have multiple texture coordinates so we always take the first set (0).
            vec.x = mesh->mTextureCoords[0][i].x;
            vec.y = mesh->mTextureCoords[0][i].y;
            vertex.TextureCoord = vec;
        }
        else
        {
            vertex.TextureCoord = Vector2( 0.0f, 0.0f );
        }
        if( mesh->mTangents )
        {
            vector.x = mesh->mTangents[i].x;
            vector.y = mesh->mTangents[i].y;
            vector.z = mesh->mTangents[i].z;
            vertex.Tangent = vector;
        }
        if( mesh->mBitangents )
        {
            vector.x = mesh->mBitangents[i].x;
            vector.y = mesh->mBitangents[i].y;
            vector.z = mesh->mBitangents[i].z;
            vertex.BiTangent = vector;
        }
        vertices.push_back( vertex );
    }

    for( unsigned int i = 0; i < mesh->mNumFaces; i++ )
    {
        aiFace face = mesh->mFaces[i];

        for( unsigned int j = 0; j < face.mNumIndices; j++ )
        {
            indices.push_back( face.mIndices[j] );
        }
    }

    SharedPtr<Moonlight::Material> newMaterial = RootNode.MaterialCache[mesh->mMaterialIndex];
    if( !newMaterial )
    {
        newMaterial = RootNode.MaterialCache[mesh->mMaterialIndex] = MakeShared<DiffuseMaterial>();

        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

        aiColor3D color( 0.f, 0.f, 0.f );
        aiColor3D color2( 1.f, 1.f, 1.f );
        material->Get( AI_MATKEY_COLOR_TRANSPARENT, color );
        if( color != color2 )
        {
            //newMaterial->RenderMode = Moonlight::RenderingMode::Transparent;
        }

        aiColor3D colorDiff( 0.f, 0.f, 0.f );
        aiColor3D colorDiff2( 0.f, 0.f, 0.f );
        material->Get( AI_MATKEY_COLOR_DIFFUSE, colorDiff );
        if( colorDiff != colorDiff2 )
        {
            newMaterial->DiffuseColor = Vector3( colorDiff.r, colorDiff.g, colorDiff.b );
        }

        LoadMaterialTextures( newMaterial, material, aiTextureType_DIFFUSE, Moonlight::TextureType::Diffuse );
        LoadMaterialTextures( newMaterial, material, aiTextureType_SPECULAR, Moonlight::TextureType::Specular );
        LoadMaterialTextures( newMaterial, material, aiTextureType_NORMALS, Moonlight::TextureType::Normal );
        LoadMaterialTextures( newMaterial, material, aiTextureType_HEIGHT, Moonlight::TextureType::Height );
        LoadMaterialTextures( newMaterial, material, aiTextureType_OPACITY, Moonlight::TextureType::Opacity );
    }

    Moonlight::MeshData* output = new Moonlight::MeshData( vertices, indices, newMaterial );
    output->Name = std::string( mesh->mName.C_Str() );
    return output;
}

bool ModelResource::LoadMaterialTextures( SharedPtr<Moonlight::Material> newMaterial, aiMaterial* mat, aiTextureType type, const Moonlight::TextureType& typeName )
{
    for( unsigned int i = 0; i < mat->GetTextureCount( type ); i++ )
    {
        aiString str;
        aiTextureMapping texMapping;
        aiTextureMapMode mapMode;
        mat->GetTexture( type, i, &str, &texMapping, nullptr, nullptr, nullptr, &mapMode );
        std::string stdString = std::string( str.C_Str() );
        if( stdString != "." )
        {
            BRUH( "Loading: " + stdString );
            std::string& texturePath = stdString;
            std::shared_ptr<Moonlight::Texture> texture;

            Moonlight::WrapMode wrapMode = Moonlight::WrapMode::Wrap;
            switch( mapMode )
            {
            case aiTextureMapMode_Wrap:
                wrapMode = Moonlight::WrapMode::Wrap;
                break;
            case aiTextureMapMode_Decal:
                wrapMode = Moonlight::WrapMode::Decal;
                break;
            case aiTextureMapMode_Mirror:
                wrapMode = Moonlight::WrapMode::Mirror;
                break;
            case aiTextureMapMode_Clamp:
                wrapMode = Moonlight::WrapMode::Clamp;
                break;
            case _aiTextureMapMode_Force32Bit:
            default:
                wrapMode = Moonlight::WrapMode::Wrap;
                break;
            }

            Path filePath( texturePath );
            if( filePath.Exists )
            {
                BRUH( "ABS: " + filePath.FullPath );
                texture = ResourceCache::GetInstance().Get<Moonlight::Texture>( filePath, wrapMode );
            }
            else
            {
                Path relativePath = Path( FilePath.GetDirectoryString() + texturePath );
                if( relativePath.Exists )
                {
                    BRUH( "REL: " + relativePath.GetLocalPathString() );
                    texture = ResourceCache::GetInstance().Get<Moonlight::Texture>( relativePath, wrapMode );
                }
            }

            // this should be done in the compile stage
            if( !texture )
            {
                Path desperationPath = ResourceCache::GetInstance().FindByName( Path( "Assets" ), Path( texturePath ).GetFileNameString( true ) );
                BRUH( "FOUND: " + desperationPath.LocalPos );
                texture = ResourceCache::GetInstance().Get<Moonlight::Texture>( desperationPath );
            }

            // this should be done in the compile stage
            if( !texture )
            {
                return false;
            }
            texture->Type = typeName;
            newMaterial->SetTexture( typeName, texture );
            return true;
        }
        YIKES_FMT( "OOPS NO TEXTURE???" );
    }
    return false;
}

void ModelResourceMetadata::OnSerialize( json& inJson )
{
}

void ModelResourceMetadata::OnDeserialize( const json& inJson )
{
}

std::string ModelResourceMetadata::GetExtension2() const
{
    return "assbin";
}

#if USING( ME_EDITOR )

void ModelResourceMetadata::OnEditorInspect()
{
    MetaBase::OnEditorInspect();

    static bool isChecked = false;
    ImGui::Checkbox( "Model Resource Test", &isChecked );
    if( ImGui::Button( "Re Export" ) )
    {
        Export();
    }
}

#endif

#if USING( ME_TOOLS )

void ScaleMeshVertices( aiMesh* mesh, float scale )
{
    for( unsigned int i = 0; i < mesh->mNumVertices; ++i )
    {
        mesh->mVertices[i] *= scale; // Scale the position
        if( mesh->HasNormals() )
        {
            mesh->mNormals[i] *= scale; // Scale the normals if needed
        }
    }
}

void ScaleSceneMeshes( const aiScene* scene, float scale )
{
    for( unsigned int i = 0; i < scene->mNumMeshes; ++i )
    {
        ScaleMeshVertices( scene->mMeshes[i], scale );
    }
}
void ModelResourceMetadata::Export()
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile( FilePath.FullPath.c_str(), aiProcess_Triangulate | aiProcess_CalcTangentSpace | aiProcess_GlobalScale | aiProcess_ConvertToLeftHanded );
    if( !scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode )
    {
        std::cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
        return;
    }


    //const char* chainStr[chain_length] = {
    //    "Cube1_$AssimpFbx$_Translation",
    //    "Cube1_$AssimpFbx$_RotationPivot",
    //    "Cube1_$AssimpFbx$_RotationPivotInverse",
    //    "Cube1_$AssimpFbx$_ScalingOffset",
    //    "Cube1_$AssimpFbx$_ScalingPivot",
    //    "Cube1_$AssimpFbx$_Scaling",
    //    "Cube1_$AssimpFbx$_ScalingPivotInverse",
    //    "Cube1"
    //};
    double factor( 0.0 );
    scene->mMetaData->Get( "UnitScaleFactor", factor );
    if( scene && scene->mRootNode )
    {
        //ScaleNode( scene->mRootNode, 0.1f ); // Example: Scale down by 50%
    }
    if( scene )
    {
        //ScaleSceneMeshes( scene, 0.1f ); // Scale down all meshes by 50%
    }
    Assimp::Exporter exporter;
    if( exporter.Export( scene, "assbin", FilePath.FullPath + ".assbin" ) != AI_SUCCESS )
    {
        std::cerr << "Error exporting model: " << exporter.GetErrorString() << std::endl;
        return;
    }
    //RootNode.Name = std::string( scene->mRootNode->mName.C_Str() );
    //ProcessNode( scene->mRootNode, scene, RootNode );
    exporter.FreeBlob();
    importer.FreeScene();
}

#endif
