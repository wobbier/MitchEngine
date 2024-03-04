#include "ShaderWriter.h"

ShaderWriter::ShaderWriter( const std::string& inShaderName )
{
    m_shaderFile = File( Path( inShaderName ) );
    m_shaderFile.Reset();
}

void ShaderWriter::Reset()
{
    m_shaderFile.Reset();
}

void ShaderWriter::WriteLine( const std::string& inLine )
{
    for ( int i = 0; i < Tabs; ++i )
    {
        m_shaderFile.Append( "\t" );
    }
    m_shaderFile.Append( inLine );
    m_shaderFile.Append( "\n" );
}

void ShaderWriter::Append( const std::string& inText )
{
    m_shaderFile.Append( inText );
}

void ShaderWriter::WriteToDisk()
{
    m_shaderFile.Write();
}

