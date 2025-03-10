#pragma once
#include <string>
#include "File.h"
#include "Math\Vector3.h"

enum class PinType;

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
    bool WriteToDisk();

    void WriteVector( Vector3 inValue );
    void WriteFloat( float inValue );
    void WriteInt( int inValue );

    void WriteTexture( Path& inPath );

    int Tabs = 0;

    std::vector<ExportedTextureInfo> m_textures;

    std::string LastVariable;
    int ID = 0;
    PinType LastType;
    std::vector<std::string> Warnings;
    std::vector<std::string> Errors;

private:
    File m_shaderFile;
};