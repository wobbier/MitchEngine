#pragma once

#include <stdint.h>
#include <Ultralight/Geometry.h>
#include "Math/Vector2.h"

#if USING( ME_PLATFORM_WIN64 )
#define ENABLE_MSAA 1
#else
#define ENABLE_MSAA 0
#endif

class GPUContext
{
public:
    GPUContext();

    virtual ~GPUContext();

    virtual void Resize( const Vector2& inSize );

    //virtual ID3D11Device* device();
    //virtual ID3D11DeviceContext* immediate_context();
    //virtual IDXGISwapChain* swap_chain();
    //virtual ID3D11RenderTargetView* render_target_view();

    virtual void EnableBlend();
    virtual void DisableBlend();

    virtual void EnableScissor();
    virtual void DisableScissor();

    virtual void SetScale( double inScale );
    virtual double GetScale() const;

    // This is in virtual units, not actual pixels.
    virtual void SetScreenSize( const Vector2& inSize );
    virtual Vector2 GetScreenSize();

    bool Initialize( int screen_width, int screen_height, double screen_scale, bool fullscreen, bool enable_vsync, bool sRGB, int samples );

private:
    double Scale = 0.0;
    Vector2 ScreenSize;
    int samples_ = 1;
};
