#include "PCH.h"
#include "GPUContext.h"

GPUContext::GPUContext()
{
}

GPUContext::~GPUContext()
{
}

void GPUContext::Resize( const Vector2& inSize )
{
    if( inSize.IsZero() )
    {
        return;
    }
    SetScreenSize( inSize );
}

void GPUContext::EnableBlend()
{

}

void GPUContext::DisableBlend()
{

}

void GPUContext::EnableScissor()
{

}

void GPUContext::DisableScissor()
{

}

void GPUContext::SetScale( double inScale )
{
    Scale = inScale;
}

double GPUContext::GetScale() const
{
    return Scale;
}

void GPUContext::SetScreenSize( const Vector2& inSize )
{
    ScreenSize = inSize;
}

Vector2 GPUContext::GetScreenSize()
{
    return ScreenSize;
}

bool GPUContext::Initialize( int screen_width, int screen_height, double screen_scale, bool fullscreen, bool enable_vsync, bool sRGB, int samples )
{
    SetScreenSize( { screen_width, screen_height } );
    Scale = screen_scale;

    return true;
}
