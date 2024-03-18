#include "ShaderGraphMaterial.h"
#include "File.h"
#include "Path.h"
#include "Resource\ResourceCache.h"
#include "Utils/ImGuiUtils.h"

void ShaderGraphMaterial::OnSerialize( json& OutJson )
{
}

void ShaderGraphMaterial::OnDeserialize( const json& InJson )
{
    if( InJson.contains( "Textures" ) )
    {
        int i = 0;
        for( const json& entry : InJson["Textures"] )
        {
            m_textures.push_back( ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( std::string( InJson["Textures"]["Path"] ) ) ) );
            s_uniforms.push_back( bgfx::createUniform( std::string( "s_texDiffuse" + std::to_string( i ) ).c_str(), bgfx::UniformType::Sampler ) );
            i++;
        }
    }
}

void ShaderGraphMaterial::LoadShader( const std::string& inShaderName )
{
    m_textures.clear();
    // clean this up
    s_uniforms.clear();
    s_uniformTextures.clear();
    Path filePath( inShaderName + ".shader" );
    if( filePath.Exists )
    {
        File shaderGraphFile( filePath );
        json parsed = json::parse( shaderGraphFile.Read() );
        OnDeserialize( parsed );
    }

    Material::LoadShader( inShaderName );
}

#if USING( ME_TOOLS )
void ShaderGraphMaterial::OnEditorInspect()
{
    if( ImGui::Button( "Load Shader" ) )
    {
        //RequestAssetSelectionEvent evt( [this]( Path selectedAsset ) {
            //std::string path = selectedAsset.GetLocalPathString();
            //size_t pos = path.rfind( selectedAsset.GetExtension() );
            //if( pos != std::string::npos ) {
            //    path.erase( pos - 1, path.length() );
            //}
            //MeshMaterial->LoadShader( path );
            //}, AssetType::Shader );
        //evt.Fire();
    }

    for( int i = 0; i < m_textures.size(); ++i )
    {
        if( ImGui::ImageButton( m_textures[i]->TexHandle, ImVec2(30, 30)) )
        {
        }
    }
}


#endif