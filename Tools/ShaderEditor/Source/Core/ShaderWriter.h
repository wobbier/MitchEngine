#pragma once
#include <string>
#include "File.h"
#include "Math\Vector3.h"

class ShaderWriter
{
public:
    ShaderWriter( const std::string& inShaderName );

    void Reset();
    void WriteLine( const std::string& inLine );
    void Append( const std::string& inText );
    void WriteToDisk();

    void WriteVector( Vector3 inValue );
    void WriteFloat( float inValue );
    void WriteInt( int inValue );

    int Tabs = 0;

    std::string LastVariable;
    int ID = 0;

private:
    File m_shaderFile;
};