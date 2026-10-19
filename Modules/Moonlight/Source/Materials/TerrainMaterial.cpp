#include "TerrainMaterial.h"
#include <cmath>
#include "Graphics/DefaultTextures.h"
#include "Core/CommandLine.h"

TerrainMaterial::TerrainMaterial() : StandardMaterial( "TerrainMaterial" )
{
    LoadShader( "Assets/Shaders/TerrainSurface" );
    m_atlasShader = MeshShader;
    const auto* caps = bgfx::getCaps();
    if( caps && !CommandLine::Has( "--terrain-texture-atlas" ) && caps->limits.maxTextureLayers >= 4 &&
        bgfx::isTextureValid( 0, false, 4, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE ) )
        m_arrayShader = Moonlight::ShaderCommand( "Assets/Shaders/TerrainSurface", "Assets/Shaders/TerrainSurfaceArray", Moonlight::ShaderLoad::Async );
    // The batch key includes every layer value and the patch weight texture.
    SupportsInstancing = true;
}


void TerrainMaterial::Init()
{
    StandardMaterial::Init();
    if( !bgfx::isValid( u_layers ) )
    {
        u_layers = bgfx::createUniform( "u_terrainLayers", bgfx::UniformType::Vec4, 4 );
        u_surface = bgfx::createUniform( "u_terrainSurface", bgfx::UniformType::Vec4 );
        u_mapping = bgfx::createUniform( "u_terrainMapping", bgfx::UniformType::Vec4, 4 );
        u_textures = bgfx::createUniform( "u_terrainTextures", bgfx::UniformType::Vec4 );
        u_origin = bgfx::createUniform( "u_terrainOrigin", bgfx::UniformType::Vec4 );
        const char* names[] = { "s_terrainBase", "s_terrainNormal", "s_terrainOrm" };
        for( unsigned i = 0; i < 3; ++i ) s_maps[i] = bgfx::createUniform( names[i], bgfx::UniformType::Sampler );
        s_weights = bgfx::createUniform( "s_terrainWeights", bgfx::UniformType::Sampler );
    }
}


bool TerrainMaterial::SetWeights( std::vector<std::array<uint8_t, 4>> weights, uint16_t side )
{
    if( !side || weights.size() != size_t( side ) * side || side > bgfx::getCaps()->limits.maxTextureSize ) return false;
    if( m_side == side && m_weightData == weights && m_weights ) return true;
    auto texture = std::make_shared<Moonlight::Texture>( Path( "" ), Moonlight::WrapMode::Clamp );
    texture->TexHandle = bgfx::createTexture2D( side, side, false, 1, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy( weights.data(), static_cast<uint32_t>( weights.size() * 4 ) ) );
    if( !bgfx::isValid( texture->TexHandle ) ) return false;
    texture->mWidth = texture->mHeight = side; texture->mChannels = 4;
    m_weights = std::move( texture ); m_weightData = std::move( weights ); m_side = side;
    return true;
}


void TerrainMaterial::SetLayerTextures( std::shared_ptr<Moonlight::TerrainTextureSet> textures )
{
    m_layerTextures = std::move( textures );
    MeshShader = m_layerTextures && m_layerTextures->IsReady() && m_layerTextures->GetLayout() == Moonlight::TerrainTextureLayout::Array ? m_arrayShader : m_atlasShader;
}


void TerrainMaterial::Use()
{
    StandardMaterial::Use();
    auto linear = Layers;
    for( auto& layer : linear ) for( size_t i = 0; i < 3; ++i )
        layer[i] = layer[i] <= 0.04045f ? layer[i] / 12.92f : std::pow( ( layer[i] + 0.055f ) / 1.055f, 2.4f );
    bgfx::setUniform( u_layers, linear.data(), 4 );
    const float params[4] = { LayersEnabled && m_weights ? 1.f : 0.f, m_side ? ( m_side - 1.f ) / m_side : 1.f, m_side ? 0.5f / m_side : 0.f, 0.f };
    bgfx::setUniform( u_surface, params );
    bgfx::setUniform( u_mapping, Mapping.data(), 4 );
    const bool textured = LayersEnabled && m_layerTextures && m_layerTextures->IsReady();
    const float textureParams[4] = { textured ? 1.f : 0.f, 512.f, 7.f, 0.f };
    const float origin[4] = { TerrainTranslation.x, TerrainTranslation.y, 0, 0 };
    bgfx::setUniform( u_textures, textureParams ); bgfx::setUniform( u_origin, origin );
    for( unsigned i = 0; i < 3; ++i )
    {
        const auto map = m_layerTextures && m_layerTextures->IsReady() ? m_layerTextures->GetMap( i ) : Moonlight::GetDefaultTexture( i == 1 ? Moonlight::DefaultTexture::FlatNormal : Moonlight::DefaultTexture::White );
        // A ready array program always receives arrays, including during diagnostic color overrides.
        bgfx::setTexture( uint8_t( i + 1 ), s_maps[i], map );
    }
    if( m_weights ) bgfx::setTexture( 0, s_weights, m_weights->TexHandle, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
}


SharedPtr<Moonlight::Material> TerrainMaterial::CreateInstance() { return MakeShared<TerrainMaterial>( *this ); }


uint64_t TerrainMaterial::GetInstanceBatchKey() const
{
    auto mix = []( uint64_t h, uint64_t v ) { return h ^ ( v + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 ) ); };
    uint64_t key = mix( StandardMaterial::GetInstanceBatchKey(), LayersEnabled );
    key = mix( key, m_weights ? m_weights->TexHandle.idx : UINT16_MAX );
    if( m_layerTextures && m_layerTextures->IsReady() ) for( unsigned i = 0; i < 3; ++i ) key = mix( key, m_layerTextures->GetMap( i ).idx );
    for( float value : { TerrainTranslation.x, TerrainTranslation.y } )
    { uint32_t bits; std::memcpy( &bits, &value, sizeof( bits ) ); key = mix( key, bits ); }
    for( const auto& mapping : Mapping ) for( float value : mapping )
    { uint32_t bits; std::memcpy( &bits, &value, sizeof( bits ) ); key = mix( key, bits ); }
    for( const auto& layer : Layers ) for( float value : layer )
    { uint32_t bits; std::memcpy( &bits, &value, sizeof( bits ) ); key = mix( key, bits ); }
    return key;
}
