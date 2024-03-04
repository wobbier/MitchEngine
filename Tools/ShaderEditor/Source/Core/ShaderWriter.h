#pragma once
#include <string>
#include "File.h"

class ShaderWriter
{
public:
    ShaderWriter( const std::string& inShaderName );

    void Reset();
    void WriteLine( const std::string& inLine );
    void Append( const std::string& inText );
    void WriteToDisk();

    int Tabs = 0;

    std::string LastVariable;
    int ID = 0;

private:
    File m_shaderFile;
};