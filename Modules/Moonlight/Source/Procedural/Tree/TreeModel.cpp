#include "TreeModel.h"
#include "Graphics/MeshData.h"
#include "Graphics/MeshLod.h"
#include "Graphics/ShaderStructures.h"
#include "Materials/FoliageMaterial.h"
#include <algorithm>
#include <cstring>

namespace MitchTree
{
    namespace
    {
        void SetTexture( FoliageMaterial& InMaterial, const TextureSource& InTextures, const std::string& InSource, bool InGenerated, Moonlight::TextureType InType, Moonlight::WrapMode InWrap )
        {
            if( InSource.empty() || !InTextures ) return;
            SharedPtr<Moonlight::Texture> texture = InTextures( InSource, InGenerated, InWrap );
            if( !texture ) return;
            texture->Type = InType;
            InMaterial.SetTexture( InType, texture );
        }


        SharedPtr<FoliageMaterial> MakeMaterial( const TreeMesh& InTree, const TreeRecipe& InRecipe, bool InLeaves, const TextureSource& InTextures )
        {
            auto material = MakeShared<FoliageMaterial>();
            material->Init();
            FoliageSettings& foliage = material->Foliage;
            foliage.Mode = InLeaves ? FoliageSettings::Part::Leaves : FoliageSettings::Part::Bark;
            foliage.TreeHeight = std::max( InTree.Height, 0.1f );
            // Wind response in metres: a flexible twig on a 12 m tree swings about 0.3 m in a strong breeze.
            foliage.TrunkLean = 0.015f * InRecipe.Wind.Trunk;
            foliage.BranchSway = 0.025f * InRecipe.Wind.Branches * foliage.TreeHeight * std::max( InTree.MaxSway, 0.f );
            foliage.LeafFlutter = InRecipe.Wind.Leaves * std::min( 0.12f * InRecipe.Leaves.Size, 0.08f );
            foliage.WindFrequency = InRecipe.Wind.Frequency;
            foliage.Variation = InRecipe.Foliage.Variation;
            if( InLeaves )
            {
                foliage.Translucency = InRecipe.Foliage.Translucency;
                material->DiffuseColor = InRecipe.Foliage.Tint;
                material->Roughness = InRecipe.Foliage.Roughness;
                material->AlphaCutoff = InRecipe.Foliage.AlphaCutoff;
                material->FaceMode = Moonlight::RenderFaceMode::Both;
                const bool generated = InRecipe.LeafTextures.Generate;
                SetTexture( *material, InTextures, generated ? "leaf-color" : InRecipe.Foliage.BaseColorMap, generated, Moonlight::TextureType::Diffuse, Moonlight::WrapMode::Clamp );
                SetTexture( *material, InTextures, generated ? "leaf-normal" : InRecipe.Foliage.NormalMap, generated, Moonlight::TextureType::Normal, Moonlight::WrapMode::Clamp );
                return material;
            }
            material->DiffuseColor = InRecipe.Bark.Tint;
            material->Roughness = InRecipe.Bark.Roughness;
            material->NormalStrength = InRecipe.Bark.NormalStrength;
            const bool generated = InRecipe.BarkTextures.Generate;
            SetTexture( *material, InTextures, generated ? "bark-color" : InRecipe.Bark.BaseColorMap, generated, Moonlight::TextureType::Diffuse, Moonlight::WrapMode::Wrap );
            SetTexture( *material, InTextures, generated ? "bark-normal" : InRecipe.Bark.NormalMap, generated, Moonlight::TextureType::Normal, Moonlight::WrapMode::Wrap );
            const std::string orm = generated ? "bark-orm" : InRecipe.Bark.OrmMap;
            SetTexture( *material, InTextures, orm, generated, Moonlight::TextureType::MetallicRoughness, Moonlight::WrapMode::Wrap );
            if( material->GetTexture( Moonlight::TextureType::MetallicRoughness ) )
            {
                SetTexture( *material, InTextures, orm, generated, Moonlight::TextureType::Occlusion, Moonlight::WrapMode::Wrap );
                // The map holds the roughness.
                material->Roughness = 1.f;
            }
            return material;
        }
    }


    std::vector<Moonlight::MeshData*> BuildMeshes( const TreeMesh& InTree, const TreeRecipe& InRecipe, const TextureSource& InTextures )
    {
        std::vector<Moonlight::MeshData*> meshes;
        const Vector3 size = InTree.BoundsMax - InTree.BoundsMin;
        const float pad = std::max( 0.1f, 0.08f * std::max( { size.x, size.y, size.z } ) );
        const AABB bounds( InTree.BoundsMin - Vector3( pad, 0.f, pad ), InTree.BoundsMax + Vector3( pad, pad, pad ) );
        for( const TreePart& part : InTree.Parts )
        {
            SharedPtr<FoliageMaterial> material = MakeMaterial( InTree, InRecipe, part.Name == "Leaves", InTextures );

            std::vector<Moonlight::PosNormTexTanBiVertex> vertices;
            std::vector<Moonlight::WindVertex> wind( part.Vertices.size() );
            vertices.reserve( part.Vertices.size() );
            for( size_t v = 0; v < part.Vertices.size(); ++v )
            {
                const TreeVertex& source = part.Vertices[v];
                Moonlight::PosNormTexTanBiVertex vertex;
                vertex.Position = source.Position;
                vertex.Normal = source.Normal;
                vertex.TextureCoord = source.UV;
                vertex.Tangent = source.Tangent;
                vertex.BiTangent = source.Bitangent;
                vertices.push_back( vertex );
                std::memcpy( wind[v].Data, source.Wind.data(), sizeof( wind[v].Data ) );
            }
            Moonlight::MeshData* mesh = new Moonlight::MeshData( vertices, part.Lods.front(), material );
            mesh->Name = part.Name;
            mesh->InitWindStream( wind );
            std::vector<Moonlight::MeshLod> lods;
            for( size_t level = 1; level < part.Lods.size() && level - 1 < InTree.LodScreenHeights.size(); ++level )
            {
                lods.push_back( { part.Lods[level], InTree.LodScreenHeights[level - 1] } );
            }
            mesh->SetLods( lods );
            mesh->Bounds = bounds;
            meshes.push_back( mesh );
        }
        return meshes;
    }
}
