#include "SkyBox.h"

#include "Graphics/ModelResource.h"
#include "Resource/ResourceCache.h"
#include "Path.h"
#include "Shaders/UnlitMaterial.h"
#include "Texture.h"


namespace Moonlight
{
    SkyBox::SkyBox( const std::string& InPath )
    {
        Path SystemPath( InPath );

        // Both load in the background (scenes create sky boxes while they load); the renderer draws
        // the sky once the mesh is there and samples the panorama once it has uploaded.
        SkyMap = ResourceCache::GetInstance().GetAsync<Texture>( SystemPath );
        SkyModel = ResourceCache::GetInstance().GetAsync<ModelResource>( Path( "Assets/Skybox/Skybox.fbx" ) );
        SkyMaterial = MakeShared<UnlitMaterial>();
        SkyMaterial->Init();
        SkyMaterial->SetTexture( TextureType::Diffuse, SkyMap );
    }

    SkyBox::~SkyBox()
    {
        //bgfx::destroy(sky)
    }

}
