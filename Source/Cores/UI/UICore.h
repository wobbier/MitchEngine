#pragma once
#include <Components/UI/BasicUIView.h>

#include <bgfx/bgfx.h>
#include <UI/OverlayManager.h>
#include <UI/FileSystemBasic.h>
#include <UI/Graphics/GPUContext.h>
#include <UI/Graphics/GPUDriver.h>
#include <Ultralight/Renderer.h>
#include <UI/FileLogger.h>
#include <UI/FontLoaderWin.h>
#include <UI/UIWindow.h>
#include <Renderer.h>
#include "Events/EventReceiver.h"

class IWindow;
class OverlayManager;
class UIDriver;

class UICore final
    : public Core<UICore>
    , public EventReceiver
{
public:
    UICore( IWindow* window, BGFXRenderer* renderer );
    ~UICore();

    virtual void Init() final;
    virtual void Update( const UpdateContext& inUpdateContext ) final;

    virtual void OnEntityAdded( Entity& NewEntity ) final;
    virtual void OnEntityRemoved( Entity& InEntity ) final;

    virtual void OnStop() override;

    void InitUIView( BasicUIView& view );

    void OnResize( const Vector2& NewSize );

    void Render();

    //OverlayManager* GetOverlayManager();

    ultralight::RefPtr<ultralight::Renderer> m_uiRenderer;

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() final;
#endif
    bgfx::TextureHandle m_uiTexture;
    Vector2 UISize;
private:
    std::vector<ultralight::RefPtr<ultralight::View>> m_views;

    BGFXRenderer* m_renderer;

    void CopyBitmapToTexture( ultralight::RefPtr<ultralight::Bitmap> bitmap );

    bool OnEvent( const BaseEvent& evt ) override;

    UniquePtr<ultralight::FileSystemBasic> m_fs;
    UniquePtr<GPUContext> m_context = nullptr;
    UniquePtr<ultralight::FontLoader> m_fontLoader = nullptr;
    UniquePtr<ultralight::FileLogger> m_logger = nullptr;
    ultralight::RefPtr<UIWindow> m_window = nullptr;
    UIDriver* m_driver = nullptr;

    bgfx::ProgramHandle UIProgram;
    bgfx::UniformHandle s_texUI;
};

