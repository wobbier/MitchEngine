#include "BGFXUtils.h"

#include "Path.h"
#include "Graphics/ShaderFile.h"
#include "Resource/ResourceCache.h"
#include "Pointers.h"

const bgfx::Memory* Moonlight::LoadMemory( const Path& filePath )
{
    bx::Error* err = nullptr;
    if( bx::open( getDefaultReader(), filePath.FullPath.c_str(), err ) )
    {
        uint32_t size = (uint32_t)bx::getSize( getDefaultReader() );
        const bgfx::Memory* mem = bgfx::alloc( size + 1 );
        bx::read( getDefaultReader(), mem->data, size );
        bx::close( getDefaultReader() );
        mem->data[mem->size - 1] = '\0';
        return mem;
    }

    if( err && !err->isOk() )
    {
        YIKES_FMT( "[%s]: %s", filePath.GetLocalPathString().c_str(), err->getMessage().getPtr() );
    }

    //if (filePath.Exists)
    //{
    //	std::vector<char> data;
    //	std::ifstream file(filePath.FullPath.c_str(), std::ios::in | std::ios::binary | std::ios::ate);
    //	data.resize(file.tellg());
    //	file.seekg(0, std::ios::beg);
    //	file.read(&data[0], data.size());

    //	return bgfx::copy(data.data(), static_cast<uint32_t>(data.size() - 1));
    //}

    YIKES( std::string( "Failed to load: " ) + std::string( filePath.FullPath ) );
    return nullptr;
}

bgfx::ShaderHandle Moonlight::LoadShader( const std::string& _name )
{
    Path finalPath = Path( _name );
    SharedPtr<ShaderFile> shad = ResourceCache::GetInstance().Get<ShaderFile>( finalPath );
    return shad->Handle;
}

bgfx::ProgramHandle Moonlight::LoadProgram( const std::string& vsName, const std::string& fsName )
{
    bgfx::ShaderHandle vertexShader = LoadShader( vsName );
    bgfx::ShaderHandle fragmentShader = BGFX_INVALID_HANDLE;
    if( fsName.size() > 0 )
    {
        fragmentShader = LoadShader( fsName );
    }

    return bgfx::createProgram( vertexShader, fragmentShader, true );
}

std::string Moonlight::GetPlatformString()
{
    switch( bgfx::getRendererType() )
    {
    case bgfx::RendererType::Noop:
    case bgfx::RendererType::Direct3D9:  return "dx9";
    case bgfx::RendererType::Direct3D11:
    case bgfx::RendererType::Direct3D12: return "dx11";
    case bgfx::RendererType::Gnm:        return "pssl";
    case bgfx::RendererType::Metal:      return "metal";
    case bgfx::RendererType::Nvn:        return "nvn";
    case bgfx::RendererType::OpenGL:     return "glsl";
    case bgfx::RendererType::OpenGLES:   return "essl";
    case bgfx::RendererType::Vulkan:
    case bgfx::RendererType::WebGPU:     return "spirv";

    case bgfx::RendererType::Count:
        BX_ASSERT( false, "You should not be here!" );
        break;
    }
    return "";
}

bx::AllocatorI* Moonlight::getDefaultAllocator()
{
    BX_PRAGMA_DIAGNOSTIC_PUSH();
    BX_PRAGMA_DIAGNOSTIC_IGNORED_MSVC( 4459 ); // warning C4459: declaration of 's_allocator' hides global declaration
    BX_PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC( "-Wshadow" );
    static bx::DefaultAllocator s_allocator;
    return &s_allocator;
    BX_PRAGMA_DIAGNOSTIC_POP();
}

bx::FileReaderI* Moonlight::getDefaultReader()
{
    BX_PRAGMA_DIAGNOSTIC_PUSH();
    BX_PRAGMA_DIAGNOSTIC_IGNORED_MSVC( 4459 ); // warning C4459: declaration of 's_allocator' hides global declaration
    BX_PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC( "-Wshadow" );
    static bx::FileReader s_reader;
    return &s_reader;
    BX_PRAGMA_DIAGNOSTIC_POP();
}
