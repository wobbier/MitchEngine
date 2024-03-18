#include "ShaderWriter.h"

ShaderWriter::ShaderWriter( const Path& inShaderName )
{
    m_shaderFile = File( inShaderName );
    m_shaderFile.Reset();
}

void ShaderWriter::Reset()
{
    m_shaderFile.Reset();
}

void ShaderWriter::WriteLine( const std::string& inLine )
{
    for( int i = 0; i < Tabs; ++i )
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

void ShaderWriter::WriteVector( Vector3 inValue )
{
    std::string var = "v3_" + std::to_string( ID++ );
    WriteLine( "vec3 " + var + " = vec3( " + std::to_string( inValue.x ) + ", " + std::to_string( inValue.y ) + ", " + std::to_string( inValue.z ) + " );" );
    LastVariable = var;
}

void ShaderWriter::WriteFloat( float inValue )
{
    std::string var = "f_" + std::to_string( ID++ );
    WriteLine( "float " + var + " = " + std::to_string( inValue ) + ";" );
    LastVariable = var;
}

void ShaderWriter::WriteInt( int inValue )
{
    std::string var = "i_" + std::to_string( ID++ );
    WriteLine( "float " + var + " = " + std::to_string( inValue ) + ";" );
    LastVariable = var;
}

void ShaderWriter::WriteTexture( Path& inPath )
{
    m_textures.push_back( { inPath.FullPath } );
}
