#pragma once
#include "CLog.h"
#include "bgfx/bgfx.h"
#include "bx/readerwriter.h"
#include "bx/file.h"
#include "Path.h"

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
            return super::open( filePath.getPtr(), _err );
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
            return super::open( filePath.getPtr(), _append, _err );
        }
    };

    bx::AllocatorI* getDefaultAllocator();
    bx::FileReaderI* getDefaultReader();

    const bgfx::Memory* LoadMemory( const Path& filePath );

    bgfx::ShaderHandle LoadShader( const std::string& _name );
    bgfx::ProgramHandle LoadProgram( const std::string& vsName, const std::string& fsName );

    std::string GetPlatformString();


    inline bool CheckAvailTransientBuffers( uint32_t inNumVertices, const bgfx::VertexLayout& inLayout, uint32_t inNumIndices )
    {
        return inNumVertices == bgfx::getAvailTransientVertexBuffer( inNumVertices, inLayout )
            && ( 0 == inNumIndices || inNumIndices == bgfx::getAvailTransientIndexBuffer( inNumIndices ) );
    }


}