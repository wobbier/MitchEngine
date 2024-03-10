#include "ShaderGraphMaterial.h"
#include "File.h"
#include "Path.h"

void ShaderGraphMaterial::OnSerialize( json& OutJson )
{
}

void ShaderGraphMaterial::OnDeserialize( const json& InJson )
{
    if( InJson.contains( "Textures" ) )
    {
        //s_uniforms.push_back()
    }
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
}

void ShaderGraphMaterial::LoadShader( const std::string& inShaderName )
{
    Path filePath( inShaderName + ".shader" );
    if( filePath.Exists )
    {
        File shaderGraphFile( filePath );
        json parsed = json::parse(shaderGraphFile.Read());
        OnDeserialize( parsed );
    }
}

#endif