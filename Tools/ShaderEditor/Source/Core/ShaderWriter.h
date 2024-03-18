#pragma once
#include <string>
#include "File.h"
#include "Math\Vector3.h"

struct ExportedTextureInfo
{
    std::string FullPath;
};

class ShaderWriter
{
public:
    ShaderWriter( const Path& inShaderName );

    void Reset();
    void WriteLine( const std::string& inLine );
    void Append( const std::string& inText );
    void WriteToDisk();

    void WriteVector( Vector3 inValue );
    void WriteFloat( float inValue );
    void WriteInt( int inValue );

    void WriteTexture( Path& inPath );

    int Tabs = 0;

    std::vector<ExportedTextureInfo> m_textures;

    std::string LastVariable;
    int ID = 0;

private:
    File m_shaderFile;
};