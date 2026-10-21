#pragma once
#include "Graphics/Material.h"

// Shows another camera's image in screen space: each pixel of the surface samples the same pixel of
// that camera's output, so a camera placed "behind" the surface looks like a window into its view
// (portals, security monitors in world space, magic mirrors). The camera should render at the main
// camera's size (Camera::MatchMainCamera); scripts link both with Mesh.ShowCameraView (C#).
//
// DiffuseColor is shown while the camera has no image to give: before it is linked, and when the
// camera would see this surface itself (a target cannot sample itself).
class PortalMaterial
    : public Moonlight::Material
{
public:
    PortalMaterial()
        : Moonlight::Material( "PortalMaterial", "Assets/Shaders/Portal" )
    {
        DiffuseColor = Vector3( 0.05f, 0.05f, 0.08f );
        // Inside faces too: stepping through puts the camera inside the surface's box.
        FaceMode = Moonlight::RenderFaceMode::Both;
    }

    void Init() final
    {
    }

    // Once linked, the renderer binds the camera's image and these parameters; unlinked shows the tint.
    void Use() final
    {
        if( ScreenViewCamera >= 0 )
        {
            return;
        }
        if( !bgfx::isValid( u_screenView ) )
        {
            u_screenView = bgfx::createUniform( "u_screenView", bgfx::UniformType::Vec4 );
        }
        const float params[4] = { DiffuseColor.x, DiffuseColor.y, DiffuseColor.z, 0.f };
        bgfx::setUniform( u_screenView, params );
    }

    bool CastsShadows() const final
    {
        return false;
    }

    SharedPtr<Material> CreateInstance() final
    {
        return MakeShared<PortalMaterial>( *this );
    }

private:
    inline static bgfx::UniformHandle u_screenView = BGFX_INVALID_HANDLE;
};

ME_REGISTER_MATERIAL_NAME( PortalMaterial, "Portal (Camera View)" )
