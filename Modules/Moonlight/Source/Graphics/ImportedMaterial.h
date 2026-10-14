#pragma once
#include "Graphics/Texture.h"
#include "Math/Vector3.h"
#include <assimp/material.h>
#include <array>
#include <algorithm>
#include <cmath>

namespace Moonlight
{
    // CPU-only interpretation of imported material properties, including Max Physical FBX maps.
    struct ImportedMaterial
    {
        Vector3 Color = Vector3( 1.f );
        Vector3 Emissive = Vector3();
        float Opacity = 1.f;
        float Metallic = 0.f;
        float Roughness = 0.5f;
        float NormalStrength = 1.f;
        float AlphaCutoff = 0.f;
        bool Transparent = false;
        bool DoubleSided = false;
        bool InvertRoughness = false;
        bool InferredNormal = false;
        std::array<std::string, TextureType::Count> Textures;
        std::array<aiTextureMapMode, TextureType::Count> Wrap;

        ImportedMaterial() { Wrap.fill( aiTextureMapMode_Wrap ); }
    };

    inline ImportedMaterial ReadImportedMaterial( const aiMaterial& InMaterial, bool InLegacy = false )
    {
        ImportedMaterial out;
        auto scalar = [&InMaterial]( const char* key, float fallback ) {
            float value = fallback;
            InMaterial.Get( key, 0, 0, value );
            return std::isfinite( value ) ? value : fallback;
        };
        auto unit = [&scalar]( const char* key, float fallback ) {
            const float value = scalar( key, fallback );
            return value >= 0.f && value <= 1.f ? value : fallback;
        };
        auto texture = [&]( aiTextureType source, TextureType target ) {
            aiString path;
            aiTextureMapMode modes[3] = { aiTextureMapMode_Wrap, aiTextureMapMode_Wrap, aiTextureMapMode_Wrap };
            if( InMaterial.GetTexture( source, 0, &path, nullptr, nullptr, nullptr, nullptr, modes ) == aiReturn_SUCCESS )
            {
                out.Textures[target] = path.C_Str();
                out.Wrap[target] = modes[0];
            }
        };
        aiColor4D base;
        aiColor3D diffuse;
        const bool hasBaseColor = InMaterial.Get( AI_MATKEY_BASE_COLOR, base ) == aiReturn_SUCCESS;
        if( hasBaseColor )
        {
            out.Color = Vector3( base.r, base.g, base.b );
            out.Opacity = base.a;
        }
        else if( InMaterial.Get( AI_MATKEY_COLOR_DIFFUSE, diffuse ) == aiReturn_SUCCESS
            && ( !InLegacy || diffuse.r + diffuse.g + diffuse.b > 0.f ) )
        {
            out.Color = Vector3( diffuse.r, diffuse.g, diffuse.b );
        }
        out.Metallic = InLegacy ? scalar( "$mat.metallicFactor", 0.f ) : unit( "$mat.metallicFactor", 0.f );
        out.Roughness = InLegacy ? scalar( "$mat.roughnessFactor", 0.5f ) : unit( "$mat.roughnessFactor", 0.5f );
        aiColor3D emissive;
        if( InMaterial.Get( AI_MATKEY_COLOR_EMISSIVE, emissive ) == aiReturn_SUCCESS ) out.Emissive = Vector3( emissive.r, emissive.g, emissive.b );
        texture( aiTextureType_BASE_COLOR, TextureType::Diffuse );
        if( out.Textures[TextureType::Diffuse].empty() ) texture( aiTextureType_DIFFUSE, TextureType::Diffuse );
        texture( aiTextureType_NORMALS, TextureType::Normal );
        texture( aiTextureType_SPECULAR, TextureType::Specular );
        texture( aiTextureType_HEIGHT, TextureType::Height );
        texture( aiTextureType_OPACITY, TextureType::Opacity );
        texture( aiTextureType_EMISSIVE, TextureType::Emissive );
        texture( aiTextureType_AMBIENT_OCCLUSION, TextureType::Occlusion );
        if( out.Textures[TextureType::Occlusion].empty() ) texture( aiTextureType_LIGHTMAP, TextureType::Occlusion );
        aiString metal, rough;
        const bool packed = InMaterial.GetTexture( aiTextureType_METALNESS, 0, &metal ) == aiReturn_SUCCESS
            && InMaterial.GetTexture( aiTextureType_DIFFUSE_ROUGHNESS, 0, &rough ) == aiReturn_SUCCESS && metal == rough;
        if( packed ) texture( aiTextureType_METALNESS, TextureType::MetallicRoughness );
        else if( !InLegacy )
        {
            texture( aiTextureType_METALNESS, TextureType::Metallic );
            texture( aiTextureType_DIFFUSE_ROUGHNESS, TextureType::Roughness );
        }
        if( InLegacy ) return out;

        if( !hasBaseColor ) out.Opacity = unit( "$mat.opacity", 1.f );
        out.DoubleSided = scalar( "$mat.twosided", 0.f ) != 0.f;
        aiString physical;
        if( InMaterial.Get( "$raw.3dsMax|ORIGINAL_MTL", 0, 0, physical ) == aiReturn_SUCCESS && physical == aiString( "PHYSICAL_MTL" ) )
        {
            auto map = [&]( const char* property, TextureType target ) {
                const std::string prefix = std::string( "$raw.3dsMax|Parameters|" ) + property;
                if( scalar( ( prefix + "_on" ).c_str(), 1.f ) == 0.f )
                {
                    out.Textures[target].clear();
                    return false;
                }
                aiString path;
                if( InMaterial.Get( ( prefix + "|file" ).c_str(), aiTextureType_UNKNOWN, 0, path ) != aiReturn_SUCCESS || path.length == 0 ) return false;
                out.Textures[target] = path.C_Str();
                return true;
            };
            // These maps replace their scalar/color controls in Max, rather than multiplying them.
            if( map( "base_color_map", TextureType::Diffuse ) ) out.Color = Vector3( 1.f );
            out.Metallic = unit( "$raw.3dsMax|Parameters|metalness", out.Metallic );
            out.Roughness = unit( "$raw.3dsMax|Parameters|roughness", out.Roughness );
            if( map( "metalness_map", TextureType::Metallic ) ) out.Metallic = 1.f;
            if( map( "roughness_map", TextureType::Roughness ) ) out.Roughness = 1.f;
            out.InvertRoughness = scalar( "$raw.3dsMax|Parameters|roughness_inv", 0.f ) != 0.f;
            if( !map( "normalCamera", TextureType::Normal ) ) map( "bump_map", TextureType::Normal );
            if( out.Textures[TextureType::Normal].empty() )
            {
                // Some Max FBX exports omit the normal-map connection but ship it beside their
                // explicitly referenced roughness map, using the same material stem.
                std::string normal = out.Textures[TextureType::Roughness];
                const size_t suffix = normal.rfind( "_Roughness" );
                if( suffix != std::string::npos )
                {
                    normal.replace( suffix, 10, "_Normal" );
                    out.Textures[TextureType::Normal] = normal;
                    out.InferredNormal = true;
                }
            }
            out.NormalStrength = std::clamp( scalar( "$raw.3dsMax|Parameters|normalCameraFactor", 1.f ), 0.f, 2.f );
            map( "cutout_map", TextureType::Opacity );
            out.Opacity = 1.f - unit( "$raw.3dsMax|Parameters|transparency", 0.f );
            out.DoubleSided = scalar( "$raw.babylonBackfaceCulling", out.DoubleSided ? 0.f : 1.f ) == 0.f;
            const float transparencyMode = scalar( "$raw.babylonTransparencyMode", 0.f );
            out.Transparent = out.Opacity < 1.f || transparencyMode == 2.f;
            if( !out.Textures[TextureType::Opacity].empty() && !out.Transparent ) out.AlphaCutoff = 0.5f;
        }
        else
        {
            aiString mode;
            InMaterial.Get( "$mat.gltf.alphaMode", 0, 0, mode );
            out.Transparent = mode != aiString( "MASK" ) && ( mode == aiString( "BLEND" ) || out.Opacity < 1.f );
            if( mode == aiString( "MASK" ) || ( !out.Textures[TextureType::Opacity].empty() && !out.Transparent ) )
                out.AlphaCutoff = unit( "$mat.gltf.alphaCutoff", 0.5f );
        }
        return out;
    }
}
