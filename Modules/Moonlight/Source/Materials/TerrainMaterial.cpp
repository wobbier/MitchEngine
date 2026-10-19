#include "TerrainMaterial.h"
#include <cmath>

TerrainMaterial::TerrainMaterial() : StandardMaterial( "TerrainMaterial" )
{
    LoadShader( "Assets/Shaders/TerrainSurface" );
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


void TerrainMaterial::Use()
{
    StandardMaterial::Use();
    auto linear = Layers;
    for( auto& layer : linear ) for( size_t i = 0; i < 3; ++i )
        layer[i] = layer[i] <= 0.04045f ? layer[i] / 12.92f : std::pow( ( layer[i] + 0.055f ) / 1.055f, 2.4f );
    bgfx::setUniform( u_layers, linear.data(), 4 );
    const float params[4] = { LayersEnabled && m_weights ? 1.f : 0.f, m_side ? ( m_side - 1.f ) / m_side : 1.f, m_side ? 0.5f / m_side : 0.f, 0.f };
    bgfx::setUniform( u_surface, params );
    if( m_weights ) bgfx::setTexture( 0, s_weights, m_weights->TexHandle, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
}


SharedPtr<Moonlight::Material> TerrainMaterial::CreateInstance() { return MakeShared<TerrainMaterial>( *this ); }


uint64_t TerrainMaterial::GetInstanceBatchKey() const
{
    auto mix = []( uint64_t h, uint64_t v ) { return h ^ ( v + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 ) ); };
    uint64_t key = mix( StandardMaterial::GetInstanceBatchKey(), LayersEnabled );
    key = mix( key, m_weights ? m_weights->TexHandle.idx : UINT16_MAX );
    for( const auto& layer : Layers ) for( float value : layer )
    { uint32_t bits; std::memcpy( &bits, &value, sizeof( bits ) ); key = mix( key, bits ); }
    return key;
}
