#include "BGFXUtils.h"

#include "Path.h"
#include "Graphics/ShaderFile.h"
#include "Resource/ResourceCache.h"
#include "Pointers.h"
#include <algorithm>
#include <memory>
#include <mutex>
#include <vector>

const bgfx::Memory* Moonlight::LoadMemory( const Path& filePath )
{
    bx::Error* err = nullptr;
    if( bx::open( getDefaultReader(), filePath.FullPath.c_str(), err ) )
    {
        uint32_t size = (uint32_t)bx::getSize( getDefaultReader() );
        const bgfx::Memory* mem = bgfx::alloc( size + 1 );
		bx::Error err2;
		bx::read(getDefaultReader(), mem->data, size, &err2);
        bx::close( getDefaultReader() );
        mem->data[mem->size - 1] = '\0';
        return mem;
    }

    if( err && !err->isOk() )
    {
        YIKES_FMT( "[%s]: %s", filePath.GetLocalPathString().c_str(), err->getMessage().getCPtr() );
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

    // The shaders are owned by their cached ShaderFile, so don't let the program destroy them.
    return bgfx::createProgram( vertexShader, fragmentShader, false );
}

namespace
{
    struct ProgramEntry
    {
        std::string Key;
        std::weak_ptr<Moonlight::ProgramRef> Ref;
        SharedPtr<Moonlight::ShaderFile> Vertex;
        SharedPtr<Moonlight::ShaderFile> Fragment;
    };

    std::vector<ProgramEntry>& ProgramRegistry()
    {
        static std::vector<ProgramEntry> registry;
        return registry;
    }

    std::mutex& ProgramRegistryMutex()
    {
        static std::mutex mutex;
        return mutex;
    }
}


Moonlight::ProgramRef::~ProgramRef()
{
    if( bgfx::isValid( Handle ) && IsGpuAlive() )
    {
        bgfx::destroy( Handle );
    }
}


SharedPtr<Moonlight::ProgramRef> Moonlight::AcquireProgram( const std::string& vsName, const std::string& fsName )
{
    std::lock_guard<std::mutex> lock( ProgramRegistryMutex() );
    std::vector<ProgramEntry>& registry = ProgramRegistry();
    registry.erase( std::remove_if( registry.begin(), registry.end(), []( const ProgramEntry& entry ) { return entry.Ref.expired(); } ), registry.end() );

    const std::string key = vsName + "|" + fsName;
    for( const ProgramEntry& entry : registry )
    {
        if( entry.Key == key )
        {
            if( SharedPtr<ProgramRef> existing = entry.Ref.lock() )
            {
                return existing;
            }
        }
    }

    ProgramEntry entry;
    entry.Key = key;
    entry.Vertex = ResourceCache::GetInstance().Get<ShaderFile>( Path( vsName ) );
    entry.Fragment = fsName.empty() ? nullptr : ResourceCache::GetInstance().Get<ShaderFile>( Path( fsName ) );
    const bgfx::ShaderHandle vertex = entry.Vertex ? entry.Vertex->Handle : bgfx::ShaderHandle( BGFX_INVALID_HANDLE );
    const bgfx::ShaderHandle fragment = entry.Fragment ? entry.Fragment->Handle : bgfx::ShaderHandle( BGFX_INVALID_HANDLE );
    SharedPtr<ProgramRef> ref = MakeShared<ProgramRef>( bgfx::isValid( vertex ) ? bgfx::createProgram( vertex, fragment, false ) : bgfx::ProgramHandle( BGFX_INVALID_HANDLE ) );
    entry.Ref = ref;
    registry.push_back( std::move( entry ) );
    return ref;
}


void Moonlight::RebuildProgramsUsing( const ShaderFile* InShader )
{
    std::lock_guard<std::mutex> lock( ProgramRegistryMutex() );
    for( ProgramEntry& entry : ProgramRegistry() )
    {
        SharedPtr<ProgramRef> ref = entry.Ref.lock();
        if( !ref || ( entry.Vertex.get() != InShader && entry.Fragment.get() != InShader ) || !entry.Vertex )
        {
            continue;
        }
        const bgfx::ProgramHandle rebuilt = bgfx::createProgram( entry.Vertex->Handle, entry.Fragment ? entry.Fragment->Handle : bgfx::ShaderHandle( BGFX_INVALID_HANDLE ), false );
        if( bgfx::isValid( rebuilt ) )
        {
            if( bgfx::isValid( ref->Handle ) )
            {
                bgfx::destroy( ref->Handle );
            }
            ref->Handle = rebuilt;
        }
    }
}


namespace
{
    bool s_gpuAlive = false;
}


bool Moonlight::IsGpuAlive()
{
    return s_gpuAlive;
}


void Moonlight::SetGpuAlive( bool InAlive )
{
    s_gpuAlive = InAlive;
}


void Moonlight::ReleaseProgramRegistry()
{
    std::lock_guard<std::mutex> lock( ProgramRegistryMutex() );
    ProgramRegistry().clear();
}


std::string Moonlight::GetPlatformString()
{
    switch( bgfx::getRendererType() )
    {
    case bgfx::RendererType::Noop:
    case bgfx::RendererType::Direct3D11:
    case bgfx::RendererType::Direct3D12: return "dx11";
    case bgfx::RendererType::Gnm:        return "pssl";
    case bgfx::RendererType::Metal:      return "metal";
    case bgfx::RendererType::Nvn:        return "nvn";
    case bgfx::RendererType::OpenGL:     return "glsl";
    case bgfx::RendererType::OpenGLES:   return "essl";
    case bgfx::RendererType::Vulkan:     return "spirv";

    case bgfx::RendererType::Count:
    default:
        break;
    }
    BX_ASSERT( false, "You should not be here!" );
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
