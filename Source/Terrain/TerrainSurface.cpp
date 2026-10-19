#include "PCH.h"
#include "TerrainSurface.h"
#include "JSON.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace TerrainData
{
    namespace
    {
        constexpr size_t kHeader = 24, kMaxSamples = 1024 * 1024;
        void Put( std::vector<uint8_t>& bytes, uint64_t value, int count )
        {
            for( int i = 0; i < count; ++i ) bytes.push_back( static_cast<uint8_t>( value >> ( 8 * i ) ) );
        }


        uint64_t Get( std::span<const uint8_t> bytes, size_t offset, int count )
        {
            uint64_t value = 0;
            for( int i = 0; i < count; ++i ) value |= uint64_t( bytes[offset + i] ) << ( 8 * i );
            return value;
        }


        uint64_t Hash( std::span<const uint8_t> bytes )
        {
            uint64_t hash = 14695981039346656037ull;
            for( uint8_t byte : bytes ) hash = ( hash ^ byte ) * 1099511628211ull;
            return hash;
        }


        bool TexturePath( const std::string& path )
        {
            if( path.empty() ) return true;
            if( path.size() > 1024 || path.front() == '/' || path.find_first_of( "\\:" ) != std::string::npos || path.find( '\0' ) != std::string::npos ) return false;
            size_t start = 0;
            while( start <= path.size() )
            {
                const size_t end = path.find( '/', start );
                const auto part = path.substr( start, end == std::string::npos ? end : end - start );
                if( part.empty() || part == "." || part == ".." ) return false;
                if( end == std::string::npos ) break;
                start = end + 1;
            }
            return true;
        }


        bool Normalized( LayerWeights weights ) { return std::accumulate( weights.begin(), weights.end(), 0 ) == 255; }
    }


    LayerWeights Surface::Get( SampleCoord sample ) const
    {
        const auto it = Samples.find( { sample.X, sample.Z } );
        return it == Samples.end() ? BaseWeights : it->second;
    }


    void Surface::Set( SampleCoord sample, LayerWeights weights )
    {
        const auto key = std::make_pair( sample.X, sample.Z );
        if( weights == BaseWeights ) Samples.erase( key ); else Samples[key] = weights;
    }


    bool Surface::Validate( const Settings& settings, std::string& error ) const
    {
        if( !Normalized( BaseWeights ) || Samples.size() > kMaxSamples )
        { error = "Terrain surface requires normalized weights and at most one million painted samples"; return false; }
        for( const auto& layer : Layers )
        {
            if( layer.Name.empty() || layer.Name.size() > 128 || layer.Name.find( '\0' ) != std::string::npos ||
                !std::isfinite( layer.Roughness ) || layer.Roughness < 0 || layer.Roughness > 1 )
            { error = "Invalid terrain surface layer name or roughness"; return false; }
            if( !TexturePath( layer.BaseColorMap ) || !TexturePath( layer.NormalMap ) || !TexturePath( layer.OrmMap ) ||
                !std::isfinite( layer.TileSize ) || layer.TileSize < 0.01f || layer.TileSize > 4096 ||
                !std::isfinite( layer.NormalStrength ) || layer.NormalStrength < 0 || layer.NormalStrength > 2 ||
                !std::isfinite( layer.Metallic ) || layer.Metallic < 0 || layer.Metallic > 1 )
            { error = "Invalid terrain texture reference, tiling or surface parameter"; return false; }
            for( float c : layer.Color ) if( !std::isfinite( c ) || c < 0 || c > 1 )
            { error = "Terrain layer colors must be finite sRGB values between zero and one"; return false; }
        }
        for( const auto& [key, weights] : Samples )
            if( !settings.Contains( SampleCoord{ key.first, key.second } ) || !Normalized( weights ) || weights == BaseWeights )
            { error = "Invalid, redundant or out-of-bounds terrain surface sample"; return false; }
        error.clear(); return true;
    }


    LayerWeights NormalizeWeights( const std::array<float, 4>& weights )
    {
        double sum = 0;
        for( float w : weights ) if( std::isfinite( w ) && w > 0 ) sum += w;
        if( sum <= 0 ) return { 255, 0, 0, 0 };
        LayerWeights result{}; std::array<double, 4> remainder{}; int total = 0;
        for( size_t i = 0; i < 4; ++i )
        {
            const double value = std::isfinite( weights[i] ) && weights[i] > 0 ? weights[i] * 255.0 / sum : 0;
            result[i] = static_cast<uint8_t>( std::floor( value ) ); total += result[i]; remainder[i] = value - result[i];
        }
        while( total++ < 255 )
        {
            const size_t index = static_cast<size_t>( std::max_element( remainder.begin(), remainder.end() ) - remainder.begin() );
            ++result[index]; remainder[index] = -1;
        }
        return result;
    }


    LayerWeights BlendLayer( LayerWeights weights, unsigned layer, float amount, bool erase )
    {
        if( layer >= 4 || !std::isfinite( amount ) ) return weights;
        amount = std::clamp( amount, 0.f, 1.f );
        std::array<float, 4> target{}, blended{};
        if( erase )
        {
            for( size_t i = 0; i < 4; ++i ) if( i != layer ) target[i] = weights[i];
            float sum = std::accumulate( target.begin(), target.end(), 0.f );
            if( sum == 0 ) target[layer == 0 ? 1 : 0] = sum = 1;
            for( float& w : target ) w = w * 255.f / sum;
        }
        else target[layer] = 255;
        for( size_t i = 0; i < 4; ++i ) blended[i] = weights[i] * ( 1 - amount ) + target[i] * amount;
        return NormalizeWeights( blended );
    }


    std::vector<LayerWeights> BuildSurfacePatch( const Surface& surface, SampleCoord origin, uint32_t quads )
    {
        if( !quads || quads > 1024 ) return {};
        std::vector<LayerWeights> result; result.reserve( size_t( quads + 1 ) * ( quads + 1 ) );
        for( uint32_t z = 0; z <= quads; ++z ) for( uint32_t x = 0; x <= quads; ++x ) result.push_back( surface.Get( { origin.X + x, origin.Z + z } ) );
        return result;
    }


    bool SerializeSurface( const Surface& surface, const Settings& settings, std::vector<uint8_t>& out, std::string& error )
    {
        try
        {
            if( !surface.Validate( settings, error ) ) return false;
            json metadata; metadata["BaseWeights"] = surface.BaseWeights; metadata["Layers"] = json::array();
            for( const auto& layer : surface.Layers ) metadata["Layers"].push_back( { { "Name", layer.Name }, { "Color", layer.Color }, { "Roughness", layer.Roughness },
                { "BaseColorMap", layer.BaseColorMap }, { "NormalMap", layer.NormalMap }, { "OrmMap", layer.OrmMap },
                { "TileSize", layer.TileSize }, { "NormalStrength", layer.NormalStrength }, { "Metallic", layer.Metallic }, { "FlipNormalY", layer.FlipNormalY } } );
            const std::string text = metadata.dump();
            if( text.size() > 32768 ) { error = "Terrain surface metadata exceeds 32 KiB"; return false; }
            std::vector<uint8_t> payload( text.begin(), text.end() );
            for( const auto& [key, weights] : surface.Samples )
            {
                Put( payload, std::bit_cast<uint64_t>( key.first ), 8 ); Put( payload, std::bit_cast<uint64_t>( key.second ), 8 );
                payload.insert( payload.end(), weights.begin(), weights.end() );
            }
            std::vector<uint8_t> bytes{ 'M', 'T', 'S', 'F' }; Put( bytes, 2, 4 ); Put( bytes, text.size(), 4 );
            Put( bytes, surface.Samples.size(), 4 ); Put( bytes, Hash( payload ), 8 ); bytes.insert( bytes.end(), payload.begin(), payload.end() );
            out = std::move( bytes ); error.clear(); return true;
        }
        catch( const std::exception& e ) { error = e.what(); return false; }
    }


    bool DeserializeSurface( std::span<const uint8_t> bytes, const Settings& settings, Surface& out, std::string& error )
    {
        try
        {
            if( bytes.size() < kHeader || Get( bytes, 0, 4 ) != 0x4653544d || ( Get( bytes, 4, 4 ) != 1 && Get( bytes, 4, 4 ) != 2 ) ) throw std::runtime_error( "Invalid terrain surface header/version" );
            const uint64_t length = Get( bytes, 8, 4 ), count = Get( bytes, 12, 4 );
            if( !length || length > 32768 || count > kMaxSamples || bytes.size() != kHeader + length + count * 20 ||
                Hash( bytes.subspan( kHeader ) ) != Get( bytes, 16, 8 ) ) throw std::runtime_error( "Invalid terrain surface length or checksum" );
            const auto value = json::parse( bytes.begin() + kHeader, bytes.begin() + kHeader + length );
            Surface surface;
            const auto& base = value.at( "BaseWeights" );
            if( !base.is_array() || base.size() != 4 ) throw std::runtime_error( "Invalid terrain base weights" );
            for( size_t i = 0; i < 4; ++i )
            {
                if( !base[i].is_number_integer() || base[i].get<int64_t>() < 0 || base[i].get<int64_t>() > 255 ) throw std::runtime_error( "Invalid terrain base weight" );
                surface.BaseWeights[i] = base[i].get<uint8_t>();
            }
            const auto& layers = value.at( "Layers" );
            if( !layers.is_array() || layers.size() != 4 ) throw std::runtime_error( "Terrain surface requires four layers" );
            for( size_t i = 0; i < 4; ++i )
            {
                const auto& item = layers[i]; auto& layer = surface.Layers[i]; layer.Name = item.at( "Name" ).get<std::string>();
                const auto& color = item.at( "Color" );
                if( !color.is_array() || color.size() != 3 ) throw std::runtime_error( "Invalid terrain layer color" );
                layer.Color = color.get<std::array<float, 3>>(); layer.Roughness = item.at( "Roughness" ).get<float>();
                if( Get( bytes, 4, 4 ) == 2 )
                {
                    layer.BaseColorMap = item.at( "BaseColorMap" ).get<std::string>(); layer.NormalMap = item.at( "NormalMap" ).get<std::string>();
                    layer.OrmMap = item.at( "OrmMap" ).get<std::string>(); layer.TileSize = item.at( "TileSize" ).get<float>();
                    layer.NormalStrength = item.at( "NormalStrength" ).get<float>(); layer.Metallic = item.at( "Metallic" ).get<float>();
                    layer.FlipNormalY = item.at( "FlipNormalY" ).get<bool>();
                }
            }
            for( size_t offset = kHeader + static_cast<size_t>( length ); offset < bytes.size(); offset += 20 )
            {
                const auto key = std::make_pair( std::bit_cast<int64_t>( Get( bytes, offset, 8 ) ), std::bit_cast<int64_t>( Get( bytes, offset + 8, 8 ) ) );
                LayerWeights weights; std::copy_n( bytes.begin() + offset + 16, 4, weights.begin() );
                if( !surface.Samples.emplace( key, weights ).second ) throw std::runtime_error( "Duplicate terrain surface sample" );
            }
            if( !surface.Validate( settings, error ) ) return false;
            out = std::move( surface ); error.clear(); return true;
        }
        catch( const std::exception& e ) { error = e.what(); return false; }
    }
}
