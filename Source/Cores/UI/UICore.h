#pragma once
#include <Components/UI/BasicUIView.h>

#if USING( ME_UI )
#include <bgfx/bgfx.h>
#include <UI/OverlayManager.h>
#include <UI/FileSystemBasic.h>
#include <UI/Graphics/GPUContext.h>
#include <UI/Graphics/GPUDriver.h>
#include <Ultralight/Renderer.h>
#include <Ultralight/platform/Config.h>
#include <UI/FileLogger.h>
#include <UI/FontLoaderWin.h>
#include <UI/UIWindow.h>
#endif

#include <Renderer.h>
#include "Events/EventReceiver.h"

class IWindow;
class OverlayManager;
class UIDriver;

class UICore final
    : public Core<UICore>
    , public EventReceiver
#if USING( ME_UI )
    , public ultralight::ViewListener
#endif
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
#if USING( ME_UI )

    void OnChangeCursor( ultralight::View* caller, ultralight::Cursor cursor ) override;

    void OnAddConsoleMessage( ultralight::View* caller, const ultralight::ConsoleMessage& message ) override;

    ultralight::RefPtr<ultralight::Renderer> m_uiRenderer;
#endif

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() final;
#endif
    bgfx::TextureHandle m_uiTexture;
    Vector2 UISize;
private:

#if USING( ME_UI )
    std::vector<ultralight::RefPtr<ultralight::View>> m_views;
#endif

    BGFXRenderer* m_renderer;

    bool OnEvent( const BaseEvent& evt ) override;


#if USING( ME_UI )
    void CopyBitmapToTexture( ultralight::RefPtr<ultralight::Bitmap> bitmap );

    UniquePtr<ultralight::FileSystemBasic> m_fs;
    UniquePtr<GPUContext> m_context = nullptr;
    UniquePtr<ultralight::FontLoader> m_fontLoader = nullptr;
    UniquePtr<ultralight::FileLogger> m_logger = nullptr;
    ultralight::RefPtr<UIWindow> m_window = nullptr;
    UIDriver* m_driver = nullptr;

    ultralight::Config m_config;
#endif

    bgfx::ProgramHandle UIProgram;
    bgfx::UniformHandle s_texUI;
};

