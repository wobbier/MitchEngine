#include "ModelResource.h"

#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/mesh.h>

#include <unordered_map>

#include "CLog.h"
#include "Resource/ResourceCache.h"
#include "Graphics/Texture.h"
#include "Graphics/Material.h"
#include "Graphics/ImportedMaterial.h"
#include "Graphics/MeshData.h"
#include "Scene/Node.h"
#include <stack>
#include "assimp/material.h"
#include "Materials/DiffuseMaterial.h"
#include "Core/Assert.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace
{
    bool IsImageExtension( std::string InExtension )
    {
        std::transform( InExtension.begin(), InExtension.end(), InExtension.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
        return InExtension == ".png" || InExtension == ".tga" || InExtension == ".jpg" || InExtension == ".jpeg";
    }


    // Model files often carry the artist's own paths (absolute, Windows-style, or into a working folder
    // that doesn't ship). Look for the texture near the model instead, from most to least certain:
    // the same file name, the same name with a shipped image format, then (asset packs that ship
    // "Texture_01.psd" as "Texture_01_A.png") the first image whose name starts with the stem.
    std::string FindTextureNear( const std::string& InModelFullPath, std::string InReference )
    {
        namespace fs = std::filesystem;
        std::replace( InReference.begin(), InReference.end(), '\\', '/' );
        const fs::path reference( InReference );
        const std::string fileName = reference.filename().string();
        const std::string stem = reference.stem().string();
        if( fileName.empty() )
        {
            return std::string();
        }
        const fs::path modelDirectory = fs::path( InModelFullPath ).parent_path();
        const fs::path directories[] = { modelDirectory, modelDirectory / "Textures", modelDirectory / ".." / "Textures", modelDirectory / ".." / "textures", modelDirectory / ".." };
        std::error_code error;
        for( const fs::path& directory : directories )
        {
            if( fs::is_regular_file( directory / fileName, error ) )
            {
                return fs::weakly_canonical( directory / fileName, error ).string();
            }
        }
        for( const fs::path& directory : directories )
        {
            for( const char* extension : { ".png", ".tga", ".jpg", ".jpeg" } )
            {
                const fs::path candidate = directory / ( stem + extension );
                if( fs::is_regular_file( candidate, error ) )
                {
                    return fs::weakly_canonical( candidate, error ).string();
                }
            }
        }
        for( const fs::path& directory : directories )
        {
            std::string best;
            for( fs::directory_iterator it( directory, error ), end; !error && it != end; it.increment( error ) )
            {
                const std::string name = it->path().filename().string();
                if( name.size() > stem.size() && name.compare( 0, stem.size(), stem ) == 0 && ( name[stem.size()] == '_' || name[stem.size()] == '-' )
                    && IsImageExtension( it->path().extension().string() ) && ( best.empty() || name < best ) )
                {
                    best = name;
                }
            }
            error.clear();
            if( !best.empty() )
            {
                return fs::weakly_canonical( directory / best, error ).string();
            }
        }
        return std::string();
    }
}


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
    return LoadAsync() && FinishAsyncLoad();
}


bool ModelResource::SupportsAsyncLoad() const
{
    return true;
}


bool ModelResource::LoadAsync()
{
    // The cooked model (tools builds cook it from the source on first use, see Export).
    Path cooked = Path( FilePath.FullPath + ".assbin" );
    if( !cooked.Exists )
    {
        CLog::Log( CLog::LogType::Error, "Model isn't cooked: " + FilePath.GetLocalPathString() );
        return false;
    }
    m_importer = std::make_unique<Assimp::Importer>();
    m_pendingScene = m_importer->ReadFile( cooked.FullPath.c_str(), 0 );
    if( !m_pendingScene || m_pendingScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !m_pendingScene->mRootNode )
    {
        CLog::Log( CLog::LogType::Error, "Model " + FilePath.GetLocalPathString() + ": " + m_importer->GetErrorString() );
        m_pendingScene = nullptr;
        m_importer.reset();
        return false;
    }
    // Clips are plain data: build them here too, and the levels of detail (index lists only).
    m_bakedImportScale = 0.f;
    if( m_pendingScene->mRootNode->mMetaData )
    {
        m_pendingScene->mRootNode->mMetaData->Get( "MitchEngineImportScale", m_bakedImportScale );
    }
    ProcessAnimations( m_pendingScene );
    // Optional baked vertex cache (e.g. a USD/Alembic animation whose FBX has only rig controls).
    // Stored in metres in right-handed source space; the OBJ cook uses ConvertToLeftHanded.
    std::ifstream cacheFile( FilePath.FullPath + ".vertexcache", std::ios::binary );
    if( cacheFile )
    {
        char magic[8];
        uint32_t headerSize = 0;
        cacheFile.read( magic, sizeof( magic ) );
        cacheFile.read( reinterpret_cast<char*>( &headerSize ), sizeof( headerSize ) );
        if( !cacheFile || std::memcmp( magic, "MEVC0001", 8 ) != 0 || headerSize > 16 * 1024 * 1024 ) return false;
        std::string headerText( headerSize, '\0' );
        cacheFile.read( headerText.data(), headerSize );
        const json header = json::parse( headerText, nullptr, false );
        if( header.is_discarded() || ( header.value( "version", 0 ) != 1 && header.value( "version", 0 ) != 2 ) ) return false;
        auto cache = MakeShared<Moonlight::VertexAnimation>();
        cache->VertexCount = header.value( "vertices", 0u );
        cache->FrameCount = header.value( "frames", 0u );
        cache->FramesPerSecond = header.value( "fps", 0.f );
        if( cache->VertexCount == 0 || cache->FrameCount < 2 || !std::isfinite( cache->FramesPerSecond ) || cache->FramesPerSecond <= 0.f ) return false;
        const uint64_t positionCount = static_cast<uint64_t>( cache->VertexCount ) * cache->FrameCount;
        const auto dataStart = cacheFile.tellg();
        cacheFile.seekg( 0, std::ios::end );
        if( cacheFile.tellg() - dataStart != static_cast<std::streamoff>( positionCount * sizeof( Vector3 ) ) ) return false;
        cacheFile.seekg( dataStart );
        cache->Positions.resize( positionCount );
        cacheFile.read( reinterpret_cast<char*>( cache->Positions.data() ), positionCount * sizeof( Vector3 ) );
        if( !cacheFile ) return false;
        const float importScale = m_bakedImportScale > 0.f ? m_bakedImportScale : 1.f;
        for( Vector3& position : cache->Positions )
        {
            position = position * importScale;
            position.z = -position.z;
        }
        cache->Neighbors = header.at( "neighbors" ).get<std::vector<std::array<uint32_t, 2>>>();
        if( cache->Neighbors.size() != cache->VertexCount ) return false;
        for( const auto& neighbors : cache->Neighbors )
            if( neighbors[0] >= cache->VertexCount || neighbors[1] >= cache->VertexCount ) return false;
        m_vertexCacheMeshes = header.at( "meshes" );
        m_vertexCacheUsesIds = header.value( "vertex_ids", std::string() ) == "color.r";
        m_vertexCache = cache;
        Moonlight::AnimationClip clip;
        clip.Name = "Baked animation";
        clip.Duration = ( cache->FrameCount - 1 ) / cache->FramesPerSecond;
        Moonlight::AnimationChannel clock;
        clock.NodeName = "__VertexAnimationClock";
        // Scale carries the clock without being mistaken for root-motion travel.
        clock.ScaleTimes = { 0.f, clip.Duration };
        clock.Scales = { Vector3( 1.f ), Vector3( 1.f + clip.Duration, 1.f, 1.f ) };
        clip.Channels.push_back( std::move( clock ) );
        m_animations.push_back( std::move( clip ) );
    }
    BuildLods( m_pendingScene );
    return true;
}


void ModelResource::BuildLods( const aiScene* inScene )
{
    m_pendingLods.clear();
    const ModelResourceMetadata* settings = dynamic_cast<const ModelResourceMetadata*>( Metadata.get() );
    if( !settings || !settings->GenerateLODs || settings->LODs.Levels <= 0 )
    {
        return;
    }
    m_pendingLods.resize( inScene->mNumMeshes );
    size_t fullTriangles = 0;
    size_t lowestTriangles = 0;
    for( unsigned int m = 0; m < inScene->mNumMeshes; ++m )
    {
        const aiMesh* mesh = inScene->mMeshes[m];
        if( mesh->mNumVertices == 0 || !mesh->mVertices )
        {
            continue;
        }
        std::vector<uint32_t> indices;
        indices.reserve( static_cast<size_t>( mesh->mNumFaces ) * 3 );
        for( unsigned int f = 0; f < mesh->mNumFaces; ++f )
        {
            const aiFace& face = mesh->mFaces[f];
            if( face.mNumIndices == 3 )
            {
                indices.insert( indices.end(), face.mIndices, face.mIndices + 3 );
            }
        }
        if( indices.size() != static_cast<size_t>( mesh->mNumFaces ) * 3 )
        {
            continue;   // points or lines mixed in: leave this mesh at full detail
        }
        // ProcessMesh keeps aiMesh vertex order, so these index the uploaded vertices.
        m_pendingLods[m] = Moonlight::BuildMeshLods( &mesh->mVertices[0].x, mesh->mNumVertices, sizeof( aiVector3D ), indices, settings->LODs );
        fullTriangles += indices.size() / 3;
        lowestTriangles += ( m_pendingLods[m].empty() ? indices.size() : m_pendingLods[m].back().Indices.size() ) / 3;
    }
    CLog::Log( CLog::LogType::Debug, "LODs for " + FilePath.GetLocalPathString() + ": " + std::to_string( fullTriangles ) + " triangles, " + std::to_string( lowestTriangles ) + " at the lowest level" );
}


bool ModelResource::FinishAsyncLoad()
{
    if( !m_pendingScene )
    {
        return false;
    }
    const aiScene* scene = m_pendingScene;
    RootNode.MaterialCache.resize( scene->mNumMaterials );
    m_allMeshData.resize( scene->mNumMeshes );

    RootNode.Name = std::string( scene->mRootNode->mName.C_Str() );
    ProcessNode( scene->mRootNode, scene, RootNode );
    if( m_vertexCache )
    {
        RootNode.Nodes.emplace_back();
        RootNode.Nodes.back().Name = "__VertexAnimationClock";
    }

    m_pendingScene = nullptr;
    m_importer.reset();
    std::vector<std::vector<Moonlight::MeshLod>>().swap( m_pendingLods );
    return true;
}


const std::vector<Moonlight::MeshData*>& ModelResource::GetAllMeshes() const
{
    return m_allMeshData;
}


const std::vector<Moonlight::AnimationClip>& ModelResource::GetAnimations() const
{
    return m_animations;
}


void ModelResource::ProcessNode( aiNode* node, const aiScene* scene, Moonlight::Node& parent )
{
    // Entities retain this hierarchy, so each transform must stay relative to its parent.
    const glm::mat4 localTransform = AssimpToGLM( node->mTransformation );

    std::string nodeName = std::string( node->mName.C_Str() );
    glm::vec3 translation;
    glm::vec3 scale;
    glm::quat rotation;
    bool wasFlipped = false;

    DecomposeMatrix( localTransform, translation, rotation, scale, wasFlipped );

    parent.Name = nodeName;
    parent.Position = Vector3( translation );
    parent.Scale = Vector3( scale );
    parent.NodeMatrix = Matrix4( localTransform );
    parent.Rotation = Quaternion( rotation );
    parent.IsFlipped = wasFlipped;

    for( unsigned int i = 0; i < node->mNumMeshes; i++ )
    {
        if( !m_allMeshData[node->mMeshes[i]] )
        {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            m_allMeshData[node->mMeshes[i]] = ProcessMesh( mesh, parent, scene );
            if( node->mMeshes[i] < m_pendingLods.size() && m_allMeshData[node->mMeshes[i]] )
            {
                m_allMeshData[node->mMeshes[i]]->SetLods( m_pendingLods[node->mMeshes[i]] );
            }
            parent.Meshes.push_back( m_allMeshData[node->mMeshes[i]] );
            continue;
        }
        parent.Meshes.push_back( m_allMeshData[node->mMeshes[i]] );
    }

    parent.Nodes.reserve( node->mNumChildren );

    for( unsigned int i = 0; i < node->mNumChildren; i++ )
    {
        parent.Nodes.emplace_back();
        ProcessNode( node->mChildren[i], scene, parent.Nodes.back() );
    }
}


void ModelResource::ProcessAnimations( const aiScene* scene )
{
    for( unsigned int i = 0; i < scene->mNumAnimations; ++i )
    {
        const aiAnimation* anim = scene->mAnimations[i];
        const double ticksPerSecond = anim->mTicksPerSecond != 0.0 ? anim->mTicksPerSecond : 25.0;

        Moonlight::AnimationClip clip;
        clip.Name = anim->mName.length > 0 ? anim->mName.C_Str() : ( "Animation " + std::to_string( i ) );
        clip.Duration = static_cast<float>( anim->mDuration / ticksPerSecond );
        clip.Channels.reserve( anim->mNumChannels );
        for( unsigned int channelIndex = 0; channelIndex < anim->mNumChannels; ++channelIndex )
        {
            const aiNodeAnim* source = anim->mChannels[channelIndex];
            Moonlight::AnimationChannel channel;
            channel.NodeName = source->mNodeName.C_Str();
            for( unsigned int k = 0; k < source->mNumPositionKeys; ++k )
            {
                const aiVectorKey& key = source->mPositionKeys[k];
                channel.PositionTimes.push_back( static_cast<float>( key.mTime / ticksPerSecond ) );
                channel.Positions.push_back( Vector3( key.mValue.x, key.mValue.y, key.mValue.z ) );
            }
            for( unsigned int k = 0; k < source->mNumRotationKeys; ++k )
            {
                const aiQuatKey& key = source->mRotationKeys[k];
                channel.RotationTimes.push_back( static_cast<float>( key.mTime / ticksPerSecond ) );
                channel.Rotations.push_back( Quaternion( key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w ) );
            }
            for( unsigned int k = 0; k < source->mNumScalingKeys; ++k )
            {
                const aiVectorKey& key = source->mScalingKeys[k];
                channel.ScaleTimes.push_back( static_cast<float>( key.mTime / ticksPerSecond ) );
                channel.Scales.push_back( Vector3( key.mValue.x, key.mValue.y, key.mValue.z ) );
            }
            // Some FBX imports retain a static pivot/pre-rotation chain but emit a combined
            // animation on its terminal node. Those keys are relative to the chain's parent;
            // applying them below the chain applies its bind transform twice. Rebase only these
            // combined channels; channels explicitly animating helper nodes already are local.
            const aiNode* node = scene->mRootNode->FindNode( source->mNodeName );
            const std::string helperPrefix = channel.NodeName + "_$AssimpFbx$_";
            aiMatrix4x4 chain;
            bool hasChain = false;
            bool animatedChain = false;
            for( const aiNode* helper = node ? node->mParent : nullptr;
                helper && std::string( helper->mName.C_Str() ).compare( 0, helperPrefix.size(), helperPrefix ) == 0;
                helper = helper->mParent )
            {
                hasChain = true;
                chain = helper->mTransformation * chain;
                for( unsigned int c = 0; c < anim->mNumChannels; ++c )
                    animatedChain = animatedChain || anim->mChannels[c]->mNodeName == helper->mName;
            }
            if( hasChain && !animatedChain )
            {
                const glm::mat4 inverseChain = glm::inverse( AssimpToGLM( chain ) );
                std::vector<float> times = channel.PositionTimes;
                times.insert( times.end(), channel.RotationTimes.begin(), channel.RotationTimes.end() );
                times.insert( times.end(), channel.ScaleTimes.begin(), channel.ScaleTimes.end() );
                std::sort( times.begin(), times.end() );
                times.erase( std::unique( times.begin(), times.end() ), times.end() );
                Moonlight::AnimationChannel local;
                local.NodeName = channel.NodeName;
                local.PositionTimes = local.RotationTimes = local.ScaleTimes = times;
                for( float time : times )
                {
                    aiVector3D bindScale, bindPosition;
                    aiQuaternion bindRotation;
                    ( chain * node->mTransformation ).Decompose( bindScale, bindRotation, bindPosition );
                    Vector3 position( bindPosition.x, bindPosition.y, bindPosition.z );
                    Quaternion rotation( bindRotation.x, bindRotation.y, bindRotation.z, bindRotation.w );
                    Vector3 scale( bindScale.x, bindScale.y, bindScale.z );
                    Moonlight::AnimationClip::Sample( channel, time, position, rotation, scale );
                    const glm::mat4 keyed = glm::translate( glm::mat4( 1.f ), position.InternalVector )
                        * glm::mat4_cast( rotation.InternalQuat ) * glm::scale( glm::mat4( 1.f ), scale.InternalVector );
                    glm::vec3 localPosition, localScale;
                    glm::quat localRotation;
                    bool flipped = false;
                    DecomposeMatrix( inverseChain * keyed, localPosition, localRotation, localScale, flipped );
                    local.Positions.emplace_back( localPosition );
                    local.Rotations.emplace_back( localRotation );
                    local.Scales.emplace_back( localScale );
                }
                channel = std::move( local );
            }
            clip.Channels.push_back( std::move( channel ) );
        }
        m_animations.push_back( std::move( clip ) );
    }
}


void ModelResource::ProcessSkin( aiMesh* mesh, Moonlight::MeshData& outMesh, const std::vector<glm::vec3>& positions )
{
    if( !mesh->HasBones() )
    {
        return;
    }
    if( mesh->mNumBones > Moonlight::MeshData::kMaxBones )
    {
        BRUH( "Skinned mesh " + std::string( mesh->mName.C_Str() ) + " has " + std::to_string( mesh->mNumBones ) + " bones (max " + std::to_string( Moonlight::MeshData::kMaxBones ) + "); drawing it unskinned" );
        return;
    }

    std::vector<Moonlight::SkinWeightsVertex> weights( mesh->mNumVertices );
    outMesh.BoneNames.resize( mesh->mNumBones );
    outMesh.BoneOffsets.resize( mesh->mNumBones );
    outMesh.BoneRadii.assign( mesh->mNumBones, 0.f );
    for( unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex )
    {
        const aiBone* bone = mesh->mBones[boneIndex];
        outMesh.BoneNames[boneIndex] = bone->mName.C_Str();
        outMesh.BoneOffsets[boneIndex] = AssimpToGLM( bone->mOffsetMatrix );
        for( unsigned int w = 0; w < bone->mNumWeights; ++w )
        {
            const aiVertexWeight& influence = bone->mWeights[w];
            if( influence.mVertexId >= mesh->mNumVertices || influence.mWeight <= 0.f )
            {
                continue;
            }
            // Keep the four strongest influences.
            Moonlight::SkinWeightsVertex& vertex = weights[influence.mVertexId];
            int slot = 0;
            for( int s = 1; s < 4; ++s )
            {
                if( vertex.Weights[s] < vertex.Weights[slot] )
                {
                    slot = s;
                }
            }
            if( influence.mWeight > vertex.Weights[slot] )
            {
                vertex.Weights[slot] = influence.mWeight;
                vertex.Indices[slot] = static_cast<uint8_t>( boneIndex );
            }
            if( influence.mWeight > 0.05f && influence.mVertexId < positions.size() )
            {
                const glm::vec3 local = glm::vec3( outMesh.BoneOffsets[boneIndex] * glm::vec4( positions[influence.mVertexId], 1.f ) );
                outMesh.BoneRadii[boneIndex] = std::max( outMesh.BoneRadii[boneIndex], glm::length( local ) );
            }
        }
    }
    // Weights sum to one; unweighted vertices keep zero weights and stay rigid in the shader.
    for( Moonlight::SkinWeightsVertex& vertex : weights )
    {
        const float total = vertex.Weights[0] + vertex.Weights[1] + vertex.Weights[2] + vertex.Weights[3];
        if( total > 0.f )
        {
            for( float& weight : vertex.Weights )
            {
                weight /= total;
            }
        }
    }
    outMesh.InitSkin( weights );
}


Moonlight::MeshData* ModelResource::ProcessMesh( aiMesh* mesh, Moonlight::Node& inParent, const aiScene* scene )
{
    std::vector<Moonlight::PosNormTexTanBiVertex> vertices;
    std::vector<uint32_t> indices;

    for( unsigned int i = 0; i < mesh->mNumVertices; i++ )
    {
        Moonlight::PosNormTexTanBiVertex vertex;
        Vector3 vector;

        vertex.Position = { mesh->mVertices[i].x,  mesh->mVertices[i].y,  mesh->mVertices[i].z };

        if( mesh->mNormals )
        {
            vector.x = mesh->mNormals[i].x;
            vector.y = mesh->mNormals[i].y;
            vector.z = mesh->mNormals[i].z;
            vertex.Normal = vector;
        }

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
        newMaterial = RootNode.MaterialCache[mesh->mMaterialIndex] = MakeShared<StandardMaterial>();
        newMaterial->Init();
        const aiMaterial& material = *scene->mMaterials[mesh->mMaterialIndex];
        const Moonlight::ImportedMaterial imported = Moonlight::ReadImportedMaterial( material );
        auto apply = []( StandardMaterial& target, const Moonlight::ImportedMaterial& data ) {
            target.DiffuseColor = data.Color;
            target.EmissiveColor = data.Emissive;
            target.Opacity = data.Opacity;
            target.Metallic = data.Metallic;
            target.Roughness = data.Roughness;
            target.NormalStrength = data.NormalStrength;
            target.AlphaCutoff = data.AlphaCutoff;
            target.InvertRoughness = data.InvertRoughness;
            target.RenderMode = data.Transparent ? Moonlight::RenderingMode::Transparent : Moonlight::RenderingMode::Opaque;
            target.FaceMode = data.DoubleSided ? Moonlight::RenderFaceMode::Both : Moonlight::RenderFaceMode::Front;
        };
        apply( *static_cast<StandardMaterial*>( newMaterial.get() ), imported );
        for( unsigned int t = 0; t < Moonlight::TextureType::Count; ++t )
        {
            if( t == Moonlight::TextureType::Normal && imported.InferredNormal
                && !Path( FilePath.GetDirectoryString() + imported.Textures[t] ).Exists
                && FindTextureNear( FilePath.FullPath, imported.Textures[t] ).empty() ) continue;
            if( !imported.Textures[t].empty() ) LoadMaterialTexture( newMaterial, imported.Textures[t], static_cast<Moonlight::TextureType>( t ), imported.Wrap[t] );
        }
        if( newMaterial->GetTexture( Moonlight::TextureType::Emissive ) && imported.Emissive.LengthSquared() <= 0.f )
            static_cast<StandardMaterial*>( newMaterial.get() )->EmissiveColor = Vector3( 1.f );

        // Saved scenes contain copies of the previous importer's defaults. Keep a comparison
        // baseline so binding fresh geometry can update those values while retaining user edits.
        const Moonlight::ImportedMaterial old = Moonlight::ReadImportedMaterial( material, true );
        auto legacy = newMaterial->CreateInstance();
        apply( *static_cast<StandardMaterial*>( legacy.get() ), old );
        legacy->GetTextures().assign( Moonlight::TextureType::Count, nullptr );
        for( unsigned int t = 0; t < Moonlight::TextureType::Count; ++t )
        {
            if( !old.Textures[t].empty() ) LoadMaterialTexture( legacy, old.Textures[t], static_cast<Moonlight::TextureType>( t ), old.Wrap[t] );
        }
        legacy->OnSerialize( newMaterial->LegacyImportDefaults );
        newMaterial->LegacyImportDefaults.erase( "InvertRoughness" );

    }

    std::vector<glm::vec3> positions;
    if( mesh->HasBones() )
    {
        positions.reserve( vertices.size() );
        for( const Moonlight::PosNormTexTanBiVertex& vertex : vertices )
        {
            positions.push_back( vertex.Position.InternalVector );
        }
    }
    Moonlight::MeshData* output = new Moonlight::MeshData( vertices, indices, newMaterial );
    output->Name = std::string( mesh->mName.C_Str() );
    ProcessSkin( mesh, *output, positions );
    if( m_vertexCache && m_vertexCacheMeshes.contains( inParent.Name ) )
    {
        const auto& range = m_vertexCacheMeshes.at( inParent.Name );
        const uint32_t first = range.at( 0 ).get<uint32_t>();
        const uint32_t count = range.at( 1 ).get<uint32_t>();
        if( count == 0 || first >= m_vertexCache->VertexCount
            || count > m_vertexCache->VertexCount - first )
        {
            delete output;
            throw std::runtime_error( "Invalid baked animation mesh: " + inParent.Name );
        }
        output->VertexCache = m_vertexCache;
        output->VertexCacheTemplate = vertices;
        for( unsigned int v = 0; v < mesh->mNumVertices; ++v )
        {
            uint32_t closest = first;
            if( m_vertexCacheUsesIds )
            {
                if( !mesh->HasVertexColors( 0 ) || !std::isfinite( mesh->mColors[0][v].r )
                    || mesh->mColors[0][v].r < 0.f || mesh->mColors[0][v].r >= m_vertexCache->VertexCount )
                {
                    delete output;
                    throw std::runtime_error( "Missing baked vertex IDs: " + inParent.Name );
                }
                closest = static_cast<uint32_t>( std::round( mesh->mColors[0][v].r ) );
                if( closest >= m_vertexCache->VertexCount )
                {
                    delete output;
                    throw std::runtime_error( "Invalid baked vertex ID: " + inParent.Name );
                }
            }
            else
            {
                // Legacy caches: locate each cooked vertex within its original source piece.
                float distance = std::numeric_limits<float>::max();
                for( uint32_t source = first; source < first + count; ++source )
                {
                    const float candidate = ( vertices[v].Position - m_vertexCache->Positions[source] ).LengthSquared();
                    if( candidate < distance ) { distance = candidate; closest = source; }
                }
            }
            if( ( vertices[v].Position - m_vertexCache->Positions[closest] ).LengthSquared() > 1e-8f )
            {
                delete output;
                throw std::runtime_error( "Baked animation does not match cooked geometry: " + inParent.Name );
            }
            output->VertexCacheIndices.push_back( closest );
            const auto& neighbors = m_vertexCache->Neighbors[closest];
            const glm::vec3 bind = m_vertexCache->Positions[closest].InternalVector;
            const glm::mat3 toFrame = glm::transpose( Moonlight::VertexAnimation::Basis(
                m_vertexCache->Positions[neighbors[0]].InternalVector - bind,
                m_vertexCache->Positions[neighbors[1]].InternalVector - bind ) );
            auto& vertex = output->VertexCacheTemplate[v];
            vertex.Normal = Vector3( toFrame * vertex.Normal.InternalVector );
            vertex.Tangent = Vector3( toFrame * vertex.Tangent.InternalVector );
            vertex.BiTangent = Vector3( toFrame * vertex.BiTangent.InternalVector );
        }
    }
    return output;
}


bool ModelResource::LoadMaterialTexture( SharedPtr<Moonlight::Material> InMaterial, const std::string& InReference, Moonlight::TextureType InType, aiTextureMapMode InWrap )
{
    if( InReference.empty() || InReference == "." ) return false;
    Moonlight::WrapMode wrap = Moonlight::WrapMode::Wrap;
    if( InWrap == aiTextureMapMode_Clamp ) wrap = Moonlight::WrapMode::Clamp;
    else if( InWrap == aiTextureMapMode_Mirror ) wrap = Moonlight::WrapMode::Mirror;
    else if( InWrap == aiTextureMapMode_Decal ) wrap = Moonlight::WrapMode::Decal;
    std::shared_ptr<Moonlight::Texture> texture;
    const Path absolute( InReference );
    const Path relative( FilePath.GetDirectoryString() + InReference );
    if( relative.Exists ) texture = ResourceCache::GetInstance().GetAsync<Moonlight::Texture>( relative, wrap );
    else if( absolute.Exists ) texture = ResourceCache::GetInstance().GetAsync<Moonlight::Texture>( absolute, wrap );
    if( !texture )
    {
        const std::string found = FindTextureNear( FilePath.FullPath, InReference );
        if( !found.empty() ) texture = ResourceCache::GetInstance().GetAsync<Moonlight::Texture>( Path( found ), wrap );
    }
#if USING( ME_TOOLS )
    if( !texture )
    {
        std::string name = InReference;
        std::replace( name.begin(), name.end(), '\\', '/' );
        name = name.substr( name.find_last_of( '/' ) + 1 );
        const Path found = ResourceCache::GetInstance().FindByName( Path( "Assets" ), name );
        if( found.Exists ) texture = ResourceCache::GetInstance().GetAsync<Moonlight::Texture>( found, wrap );
    }
#endif
    if( !texture )
    {
        BRUH( "Model " + FilePath.GetLocalPathString() + ": texture '" + InReference + "' not found" );
        return false;
    }
    texture->Type = InType;
    InMaterial->SetTexture( InType, texture );
    return true;
}


void ModelResourceMetadata::OnSerialize( json& inJson )
{
    if( ImportScale != 1.f )
    {
        inJson["ImportScale"] = ImportScale;
    }
    // Always store this setting: old sidecars omitted it when conversion was opt-in.
    inJson["ConvertUnits"] = ConvertUnits;
    if( GenerateLODs )
    {
        // Rounded, so a float like 0.02 isn't saved as 0.019999999552965164.
        auto tidy = []( float InValue ) { return std::round( static_cast<double>( InValue ) * 1e6 ) / 1e6; };
        inJson["GenerateLODs"] = true;
        inJson["LODLevels"] = LODs.Levels;
        inJson["LODReduction"] = tidy( LODs.Reduction );
        inJson["LODTransition"] = tidy( LODs.TransitionHeight );
        inJson["LODMaxError"] = tidy( LODs.MaxError );
    }
}


void ModelResourceMetadata::OnDeserialize( const json& inJson )
{
    ImportScale = inJson.value( "ImportScale", 1.f );
    ConvertUnits = inJson.value( "ConvertUnits", true );
    // Rebuild old cooks once, even when the source timestamp hasn't changed. Explicit opt-outs
    // remain valid for files with incorrect unit metadata.
    FlaggedForExport = FlaggedForExport || !inJson.contains( "ConvertUnits" );
    const Moonlight::MeshLodSettings defaults;
    GenerateLODs = inJson.value( "GenerateLODs", false );
    LODs.Levels = std::clamp( inJson.value( "LODLevels", defaults.Levels ), 0, 4 );
    LODs.Reduction = inJson.value( "LODReduction", defaults.Reduction );
    LODs.TransitionHeight = inJson.value( "LODTransition", defaults.TransitionHeight );
    LODs.MaxError = inJson.value( "LODMaxError", defaults.MaxError );
}


float ModelResourceMetadata::GetBakedScale( double InUnitScaleFactor ) const
{
    const double units = ConvertUnits && InUnitScaleFactor > 0.0 ? InUnitScaleFactor / 100.0 : 1.0;
    return static_cast<float>( units ) * ImportScale;
}


std::string ModelResourceMetadata::GetExtension2() const
{
    return "assbin";
}


#if USING( ME_EDITOR )

void ModelResourceMetadata::OnEditorInspect()
{
    MetaBase::OnEditorInspect();

    ImGui::DragFloat( "Import Scale", &ImportScale, 0.001f, 0.0001f, 10000.f, "%.4f" );
    ImGui::Checkbox( "Convert Units To Metres", &ConvertUnits );
    if( ImGui::IsItemHovered() )
    {
        ImGui::SetTooltip( "Use the file's unit (FBX UnitScaleFactor): centimetre assets import at metre size" );
    }
    ImGui::Checkbox( "Generate LODs", &GenerateLODs );
    if( ImGui::IsItemHovered() )
    {
        ImGui::SetTooltip( "Simplified versions of each mesh, drawn when it's small on screen (meshoptimizer)" );
    }
    if( GenerateLODs )
    {
        ImGui::SliderInt( "LOD Levels", &LODs.Levels, 1, 4 );
        ImGui::SliderFloat( "Triangles Kept Per Level", &LODs.Reduction, 0.1f, 0.9f, "%.2f" );
        ImGui::SliderFloat( "First LOD Below Screen Height", &LODs.TransitionHeight, 0.01f, 1.f, "%.2f" );
        if( ImGui::IsItemHovered() )
        {
            ImGui::SetTooltip( "Level 1 draws when the mesh covers less than this fraction of the view's height; each further level at half that" );
        }
        ImGui::SliderFloat( "Max Error", &LODs.MaxError, 0.001f, 0.2f, "%.3f" );
        if( ImGui::IsItemHovered() )
        {
            ImGui::SetTooltip( "Largest deviation of level 1, relative to the mesh's size (doubles each level)" );
        }
    }
    ImGui::TextDisabled( "Apply re-imports; reopen scenes that use the model to see the change" );
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


namespace
{
    void ScaleNodeTranslations( aiNode* InNode, float InScale )
    {
        InNode->mTransformation.a4 *= InScale;
        InNode->mTransformation.b4 *= InScale;
        InNode->mTransformation.c4 *= InScale;
        for( unsigned int i = 0; i < InNode->mNumChildren; ++i )
        {
            ScaleNodeTranslations( InNode->mChildren[i], InScale );
        }
    }


    // A uniform scale baked into the data: every node's translation, every vertex, every bone's
    // offset translation and every animation position key. Linear parts commute with a uniform
    // scale, so the whole model scales without any scale in its transforms.
    void BakeImportScale( aiScene& InScene, float InScale )
    {
        ScaleNodeTranslations( InScene.mRootNode, InScale );
        for( unsigned int m = 0; m < InScene.mNumMeshes; ++m )
        {
            aiMesh* mesh = InScene.mMeshes[m];
            for( unsigned int v = 0; v < mesh->mNumVertices; ++v )
            {
                mesh->mVertices[v] *= InScale;
            }
            for( unsigned int b = 0; b < mesh->mNumBones; ++b )
            {
                aiMatrix4x4& offset = mesh->mBones[b]->mOffsetMatrix;
                offset.a4 *= InScale;
                offset.b4 *= InScale;
                offset.c4 *= InScale;
            }
        }
        for( unsigned int a = 0; a < InScene.mNumAnimations; ++a )
        {
            aiAnimation* animation = InScene.mAnimations[a];
            for( unsigned int c = 0; c < animation->mNumChannels; ++c )
            {
                aiNodeAnim* channel = animation->mChannels[c];
                for( unsigned int k = 0; k < channel->mNumPositionKeys; ++k )
                {
                    channel->mPositionKeys[k].mValue *= InScale;
                }
            }
        }
    }
}


void ModelResourceMetadata::Export()
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile( FilePath.FullPath.c_str(), aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace | aiProcess_LimitBoneWeights | aiProcess_ConvertToLeftHanded );
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
    // FBX: centimetres per unit (Assimp stores it as a float or a double depending on the file).
    double factor( 0.0 );
    if( scene->mMetaData )
    {
        float factorFloat = 0.f;
        if( scene->mMetaData->Get( "UnitScaleFactor", factorFloat ) )
        {
            factor = factorFloat;
        }
        else
        {
            scene->mMetaData->Get( "UnitScaleFactor", factor );
        }
    }
    const float scale = GetBakedScale( factor );
    if( scale > 0.f && std::abs( scale - 1.f ) > 1e-6f )
    {
        // The importer owns the scene; it's ours to change until it's exported.
        BakeImportScale( *const_cast<aiScene*>( scene ), scale );
    }
    // assbin preserves node metadata (not scene metadata). Remember the scale that actually
    // produced this geometry so saved model instances can rescale their existing translations.
    aiMetadata*& metadata = scene->mRootNode->mMetaData;
    if( !metadata ) metadata = new aiMetadata();
    metadata->Add( "MitchEngineImportScale", scale );
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
