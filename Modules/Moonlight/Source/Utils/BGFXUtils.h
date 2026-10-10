#pragma once
#include "CLog.h"
#include "bgfx/bgfx.h"
#include "bx/readerwriter.h"
#include "bx/file.h"
#include "Path.h"
#include "Pointers.h"

namespace Moonlight
{
    static bx::FileReaderI* s_fileReader;
    static bx::FileWriterI* s_fileWriter;

    static bx::AllocatorI* s_allocator3 = new bx::DefaultAllocator();
    typedef bx::StringT<&s_allocator3> String;
    static String s_currentDir;

    class FileReader : public bx::FileReader
    {
        typedef bx::FileReader super;

    public:
        virtual bool open( const bx::FilePath& _filePath, bx::Error* _err ) override
        {
            String filePath( s_currentDir );
            filePath.append( _filePath );
            return super::open( filePath.getCPtr(), _err );
        }
    };

    class FileWriter : public bx::FileWriter
    {
        typedef bx::FileWriter super;

    public:
        virtual bool open( const bx::FilePath& _filePath, bool _append, bx::Error* _err ) override
        {
            String filePath( s_currentDir );
            filePath.append( _filePath );
            return super::open( filePath.getCPtr(), _append, _err );
        }
    };

    bx::AllocatorI* getDefaultAllocator();
    bx::FileReaderI* getDefaultReader();

    const bgfx::Memory* LoadMemory( const Path& filePath );

    class ShaderFile;

    bgfx::ShaderHandle LoadShader( const std::string& _name );
    // A raw program (not hot reloaded; prefer ShaderCommand / AcquireProgram).
    bgfx::ProgramHandle LoadProgram( const std::string& vsName, const std::string& fsName );

    // One owned reference to a bgfx program. Shared by everyone using the same shader pair, and
    // rebuilt in place when one of its shaders hot reloads, so always read Handle at submit time.
    struct ProgramRef
    {
        ProgramRef() = default;
        explicit ProgramRef( bgfx::ProgramHandle InHandle ) : Handle( InHandle ) {}
        ~ProgramRef();
        ProgramRef( const ProgramRef& ) = delete;
        ProgramRef& operator=( const ProgramRef& ) = delete;
        bgfx::ProgramHandle Handle = BGFX_INVALID_HANDLE;
    };

    SharedPtr<ProgramRef> AcquireProgram( const std::string& vsName, const std::string& fsName );
    // Recreates every acquired program that uses InShader (called after it reloads).
    void RebuildProgramsUsing( const ShaderFile* InShader );
    // Drops the registry's shader references (before the renderer shuts down).
    void ReleaseProgramRegistry();

    std::string GetPlatformString();


    // Packs a 0..1 RGB color into bgfx's 0xRRGGBBAA clear format, clamping each channel.
    inline uint32_t PackClearColor( float r, float g, float b, float a = 1.f )
    {
        auto toByte = []( float v ) { return static_cast<uint32_t>( bx::clamp( v, 0.f, 1.f ) * 255.f + 0.5f ); };
        return toByte( r ) << 24 | toByte( g ) << 16 | toByte( b ) << 8 | toByte( a );
    }


    inline bool CheckAvailTransientBuffers( uint32_t inNumVertices, const bgfx::VertexLayout& inLayout, uint32_t inNumIndices )
    {
        return inNumVertices == bgfx::getAvailTransientVertexBuffer( inNumVertices, inLayout )
            && ( 0 == inNumIndices || inNumIndices == bgfx::getAvailTransientIndexBuffer( inNumIndices ) );
    }


}

namespace Moonlight
{
    // False before bgfx::init and after bgfx::shutdown. GPU objects released then (static caches,
    // the game object at exit) skip their bgfx calls instead of touching a dead context.
    bool IsGpuAlive();
    void SetGpuAlive( bool InAlive );
}
