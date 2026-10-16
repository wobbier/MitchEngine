#include "ShaderGraphPreview.h"

#if USING( ME_EDITOR )
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "Graphics/DefaultTextures.h"
#include "Graphics/ShaderCompiler.h"
#include "Graphics/Texture.h"
#include "Path.h"
#include "Resource/ResourceCache.h"
#include "ShaderGraph/Graph.h"
#include "ShaderGraph/NodeDef.h"
#include "Utils/BGFXUtils.h"

using namespace ShaderGraph;

namespace
{
    struct PreviewVertex
    {
        float X, Y, Z;
        float U, V;
    };


    std::vector<char> ReadBytes( const std::string& InPath )
    {
        std::ifstream file( InPath, std::ios::binary | std::ios::ate );
        std::vector<char> bytes;
        if( !file )
        {
            return bytes;
        }
        bytes.resize( static_cast<size_t>( file.tellg() ) );
        file.seekg( 0 );
        file.read( bytes.data(), static_cast<std::streamsize>( bytes.size() ) );
        return bytes;
    }


    bgfx::ShaderHandle CreateShader( const std::vector<char>& InBinary )
    {
        // Null-terminated like the engine's shader loads: some backends read the binary as text.
        const bgfx::Memory* memory = bgfx::alloc( static_cast<uint32_t>( InBinary.size() + 1 ) );
        std::memcpy( memory->data, InBinary.data(), InBinary.size() );
        memory->data[InBinary.size()] = '\0';
        return bgfx::createShader( memory );
    }
}


ShaderGraphPreview::ShaderGraphPreview( const std::string& InGraphPath )
{
    m_workDirectory = Path( ".tmp/ShaderGraphPreview/" + SanitizeIdentifier( InGraphPath ) ).FullPath;
    for( bgfx::UniformHandle& sampler : s_textures )
    {
        sampler = BGFX_INVALID_HANDLE;
    }
    for( int tile = kAtlasSize / kTileSize * ( kAtlasSize / kTileSize ) - 1; tile >= 0; --tile )
    {
        m_freeTiles.push_back( tile );
    }
}


ShaderGraphPreview::~ShaderGraphPreview()
{
    if( m_job.valid() )
    {
        m_job.wait();
    }
    if( !Moonlight::IsGpuAlive() )
    {
        return;
    }
    for( bgfx::ProgramHandle* program : { &m_program } )
    {
        if( bgfx::isValid( *program ) )
        {
            bgfx::destroy( *program );
        }
    }
    for( bgfx::FrameBufferHandle* frameBuffer : { &m_atlas, &m_main } )
    {
        if( bgfx::isValid( *frameBuffer ) )
        {
            bgfx::destroy( *frameBuffer );   // destroys its texture too
        }
    }
    for( bgfx::UniformHandle* uniform : { &u_preview, &u_select, &u_params } )
    {
        if( bgfx::isValid( *uniform ) )
        {
            bgfx::destroy( *uniform );
        }
    }
    for( bgfx::UniformHandle& sampler : s_textures )
    {
        if( bgfx::isValid( sampler ) )
        {
            bgfx::destroy( sampler );
        }
    }
}


void ShaderGraphPreview::CreateResources()
{
    if( bgfx::isValid( m_atlas ) || !Moonlight::IsGpuAlive() )
    {
        return;
    }
    const uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    m_atlasTexture = bgfx::createTexture2D( kAtlasSize, kAtlasSize, false, 1, bgfx::TextureFormat::RGBA8, flags );
    bgfx::setName( m_atlasTexture, "Shader Graph Node Previews" );
    m_atlas = bgfx::createFrameBuffer( 1, &m_atlasTexture, true );
    m_mainTexture = bgfx::createTexture2D( kMainSize, kMainSize, false, 1, bgfx::TextureFormat::RGBA8, flags );
    bgfx::setName( m_mainTexture, "Shader Graph Main Preview" );
    m_main = bgfx::createFrameBuffer( 1, &m_mainTexture, true );

    u_preview = bgfx::createUniform( "u_sgPreview", bgfx::UniformType::Vec4, kMaxPreviewConstants );
    u_select = bgfx::createUniform( "u_sgPreviewSelect", bgfx::UniformType::Vec4 );
    u_params = bgfx::createUniform( "u_sgParams", bgfx::UniformType::Vec4, kMaxParameters );
    for( int i = 0; i < kMaxTextures; ++i )
    {
        s_textures[i] = bgfx::createUniform( ( "s_sgTex" + std::to_string( i ) ).c_str(), bgfx::UniformType::Sampler );
    }
    m_layout.begin()
        .add( bgfx::Attrib::Position, 3, bgfx::AttribType::Float )
        .add( bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float )
        .end();
}


void ShaderGraphPreview::Update( const CompileResult& InPreview, const Graph& InGraph )
{
    const GeneratedFile* fragment = InPreview.FindFile( "Preview.frag" );
    const GeneratedFile* vertex = InPreview.FindFile( "Preview.vert" );
    const GeneratedFile* varyings = InPreview.FindFile( "Preview.var" );
    if( !fragment || !vertex || !varyings )
    {
        return;
    }
    Snapshot snapshot;
    snapshot.Fragment = fragment->Source;
    snapshot.Vertex = vertex->Source;
    snapshot.Varyings = varyings->Source;
    snapshot.Outputs = InPreview.PreviewOutputs;
    snapshot.Constants.assign( kMaxPreviewConstants, Vector4() );
    for( const PreviewConstant& constant : InPreview.PreviewConstants )
    {
        if( constant.Slot >= 0 && constant.Slot < kMaxPreviewConstants )
        {
            snapshot.Constants[constant.Slot] = constant.Value;
        }
    }
    for( Vector4& parameter : snapshot.Parameters )
    {
        parameter = Vector4();
    }
    for( const ParameterSlot& parameter : InPreview.Parameters )
    {
        Vector4 value = parameter.Default;
        const Property* property = InGraph.FindProperty( parameter.PropertyId );
        if( property && property->IsColor )
        {
            value = Vector4( SrgbToLinear( value.x ), SrgbToLinear( value.y ), SrgbToLinear( value.z ), value.w );
        }
        snapshot.Parameters[parameter.Slot] = value;
    }
    for( const TextureSlot& texture : InPreview.Textures )
    {
        const bool cube = texture.Type == ValueType::TextureCube;
        if( !texture.Asset.empty() )
        {
            snapshot.Textures.push_back( { texture.Slot, ResourceCache::GetInstance().GetAsync<Moonlight::Texture>( Path( texture.Asset ) ) } );
        }
        const Moonlight::DefaultTexture fallback = cube ? Moonlight::DefaultTexture::BlackCube : Moonlight::DefaultTextureFromName( texture.DefaultTexture );
        snapshot.Fallbacks.push_back( { texture.Slot, Moonlight::GetDefaultTexture( fallback ) } );
    }
    for( const Node& node : InGraph.Nodes )
    {
        if( node.PreviewMode != 0 )
        {
            snapshot.Modes[node.Id] = node.PreviewMode;
        }
    }
    snapshot.Animated = snapshot.Fragment.find( "sg_Time" ) != std::string::npos || snapshot.Fragment.find( "sg_DeltaTime" ) != std::string::npos;

    // Tiles follow the nodes: new nodes get one, deleted nodes give theirs back.
    std::map<uint32_t, int> tiles;
    for( const PreviewOutput& output : snapshot.Outputs )
    {
        auto existing = m_tiles.find( output.NodeId );
        if( existing != m_tiles.end() )
        {
            tiles[output.NodeId] = existing->second;
            m_tiles.erase( existing );
        }
    }
    for( const auto& stale : m_tiles )
    {
        m_freeTiles.push_back( stale.second );
    }
    for( const PreviewOutput& output : snapshot.Outputs )
    {
        if( !tiles.count( output.NodeId ) && !m_freeTiles.empty() )
        {
            tiles[output.NodeId] = m_freeTiles.back();
            m_freeTiles.pop_back();
        }
    }
    m_tiles = std::move( tiles );

    m_latest = std::move( snapshot );
    if( bgfx::isValid( m_program ) && m_latest.Fragment == m_active.Fragment )
    {
        // Same code, new values: no compile.
        m_active = m_latest;
        m_dirty = true;
    }
    else if( !m_job.valid() )
    {
        StartCompile();
    }
}


void ShaderGraphPreview::StartCompile()
{
    m_compiling = m_latest;
    // A few rotating folders: a compile never writes over the one being read.
    const std::string directory = m_workDirectory + "/" + std::to_string( ++m_generation % 3 );
    const std::string fragment = m_compiling.Fragment;
    const std::string vertex = m_compiling.Vertex;
    const std::string varyings = m_compiling.Varyings;
    m_job = std::async( std::launch::async, [directory, fragment, vertex, varyings]() {
        JobResult result;
        std::error_code error;
        std::filesystem::create_directories( directory, error );
        auto write = [&directory]( const char* InName, const std::string& InText ) {
            std::ofstream file( directory + "/" + InName, std::ios::binary | std::ios::trunc );
            file << InText;
        };
        write( "Preview.frag", fragment );
        write( "Preview.vert", vertex );
        write( "Preview.var", varyings );
        for( const char* name : { "Preview.vert", "Preview.frag" } )
        {
            Moonlight::ShaderCompiler::Request request;
            request.SourcePath = directory + "/" + name;
            request.OutputPath = request.SourcePath + ".bin";
            request.ShaderStage = Moonlight::ShaderCompiler::StageFromPath( request.SourcePath );
            const Moonlight::ShaderCompiler::Result compiled = Moonlight::ShaderCompiler::Compile( request );
            if( !compiled.Success )
            {
                result.Log += compiled.Log;
                return result;
            }
            ( request.ShaderStage == Moonlight::ShaderCompiler::Stage::Vertex ? result.Vertex : result.Fragment ) = ReadBytes( request.OutputPath );
        }
        result.Success = !result.Vertex.empty() && !result.Fragment.empty();
        return result;
    } );
}


void ShaderGraphPreview::FinishCompile()
{
    JobResult result = m_job.get();
    if( result.Success && Moonlight::IsGpuAlive() )
    {
        const bgfx::ShaderHandle vertex = CreateShader( result.Vertex );
        const bgfx::ShaderHandle fragment = CreateShader( result.Fragment );
        const bgfx::ProgramHandle program = bgfx::createProgram( vertex, fragment, true );
        if( bgfx::isValid( program ) )
        {
            if( bgfx::isValid( m_program ) )
            {
                bgfx::destroy( m_program );
            }
            m_program = program;
            m_active = m_compiling;
            ++m_programCount;
            m_shaderErrors.clear();
            m_dirty = true;
        }
    }
    else
    {
        m_shaderErrors = MapShaderErrors( result.Log );
    }
    if( m_latest.Fragment == m_active.Fragment && bgfx::isValid( m_program ) )
    {
        m_active = m_latest;
    }
    else if( m_latest.Fragment != m_compiling.Fragment )
    {
        // Edited while compiling: compile the latest.
        StartCompile();
    }
}


void ShaderGraphPreview::DrawQuad( bgfx::ViewId InView, float InX, float InY, float InSize, float InTargetSize, float InSelect, float InMode )
{
    if( bgfx::getAvailTransientVertexBuffer( 4, m_layout ) < 4 )
    {
        return;
    }
    bgfx::TransientVertexBuffer buffer;
    bgfx::allocTransientVertexBuffer( &buffer, 4, m_layout );
    PreviewVertex* vertices = reinterpret_cast<PreviewVertex*>( buffer.data );
    const float flip = bgfx::getCaps()->originBottomLeft ? -1.f : 1.f;
    const float left = InX / InTargetSize * 2.f - 1.f;
    const float right = ( InX + InSize ) / InTargetSize * 2.f - 1.f;
    const float top = ( 1.f - InY / InTargetSize * 2.f ) * flip;
    const float bottom = ( 1.f - ( InY + InSize ) / InTargetSize * 2.f ) * flip;
    vertices[0] = { left, top, 0.f, 0.f, 0.f };
    vertices[1] = { right, top, 0.f, 1.f, 0.f };
    vertices[2] = { left, bottom, 0.f, 0.f, 1.f };
    vertices[3] = { right, bottom, 0.f, 1.f, 1.f };
    bgfx::setVertexBuffer( 0, &buffer );

    const float select[4] = { InSelect, InMode, 0.f, 0.f };
    bgfx::setUniform( u_select, select );
    bgfx::setUniform( u_preview, m_active.Constants.data(), kMaxPreviewConstants );
    bgfx::setUniform( u_params, m_active.Parameters, kMaxParameters );
    for( const auto& fallback : m_active.Fallbacks )
    {
        bgfx::TextureHandle handle = fallback.second;
        for( const auto& texture : m_active.Textures )
        {
            if( texture.first == fallback.first && texture.second && bgfx::isValid( texture.second->TexHandle ) )
            {
                handle = texture.second->TexHandle;
            }
        }
        if( bgfx::isValid( handle ) )
        {
            bgfx::setTexture( static_cast<uint8_t>( fallback.first ), s_textures[fallback.first], handle );
        }
    }
    bgfx::setState( BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_PT_TRISTRIP );
    bgfx::submit( InView, m_program );
}


void ShaderGraphPreview::Render( bgfx::ViewId InTileView, bgfx::ViewId InMainView )
{
    CreateResources();
    if( m_job.valid() && m_job.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready )
    {
        FinishCompile();
    }
    if( !bgfx::isValid( m_program ) || !bgfx::isValid( m_atlas ) )
    {
        return;
    }
    // Textures still streaming in: keep redrawing until they arrive.
    for( const auto& texture : m_active.Textures )
    {
        if( texture.second && !bgfx::isValid( texture.second->TexHandle ) && !texture.second->HasLoadFailed() )
        {
            m_dirty = true;
        }
    }
    if( !m_dirty && !m_active.Animated )
    {
        return;
    }
    m_dirty = false;

    bgfx::setViewName( InTileView, "Shader Graph Previews" );
    bgfx::setViewFrameBuffer( InTileView, m_atlas );
    bgfx::setViewRect( InTileView, 0, 0, kAtlasSize, kAtlasSize );
    bgfx::setViewClear( InTileView, BGFX_CLEAR_NONE );
    bgfx::setViewMode( InTileView, bgfx::ViewMode::Sequential );
    const int columns = kAtlasSize / kTileSize;
    for( const PreviewOutput& output : m_active.Outputs )
    {
        auto tile = m_tiles.find( output.NodeId );
        if( tile == m_tiles.end() )
        {
            continue;
        }
        auto mode = m_active.Modes.find( output.NodeId );
        const float sphere = mode != m_active.Modes.end() ? ( mode->second == 2 ? 1.f : 0.f ) : ( output.Spatial ? 1.f : 0.f );
        const float x = static_cast<float>( ( tile->second % columns ) * kTileSize );
        const float y = static_cast<float>( ( tile->second / columns ) * kTileSize );
        DrawQuad( InTileView, x, y, kTileSize, kAtlasSize, static_cast<float>( output.Index ), sphere );
    }

    bgfx::setViewName( InMainView, "Shader Graph Main Preview" );
    bgfx::setViewFrameBuffer( InMainView, m_main );
    bgfx::setViewRect( InMainView, 0, 0, kMainSize, kMainSize );
    bgfx::setViewClear( InMainView, BGFX_CLEAR_NONE );
    DrawQuad( InMainView, 0.f, 0.f, kMainSize, kMainSize, -1.f, 1.f );
    m_mainRendered = true;
}


bool ShaderGraphPreview::GetTile( uint32_t InNode, ImVec2& OutUv0, ImVec2& OutUv1 ) const
{
    auto tile = m_tiles.find( InNode );
    if( tile == m_tiles.end() || !bgfx::isValid( m_program ) )
    {
        return false;
    }
    bool shown = false;
    for( const PreviewOutput& output : m_active.Outputs )
    {
        shown = shown || output.NodeId == InNode;
    }
    if( !shown )
    {
        return false;
    }
    const int columns = kAtlasSize / kTileSize;
    const float size = static_cast<float>( kAtlasSize );
    const float x = static_cast<float>( ( tile->second % columns ) * kTileSize );
    const float y = static_cast<float>( ( tile->second / columns ) * kTileSize );
    OutUv0 = ImVec2( x / size, y / size );
    OutUv1 = ImVec2( ( x + kTileSize ) / size, ( y + kTileSize ) / size );
    return true;
}


bool ShaderGraphPreview::GetMain( ImVec2& OutUv0, ImVec2& OutUv1 ) const
{
    OutUv0 = ImVec2( 0.f, 0.f );
    OutUv1 = ImVec2( 1.f, 1.f );
    return m_mainRendered && bgfx::isValid( m_program );
}
#endif
