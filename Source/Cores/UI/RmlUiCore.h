#pragma once
#include "ECS/Core.h"
#include "Dementia.h"
#include "Events/EventReceiver.h"
#include "Math/Vector2.h"

#include <bgfx/bgfx.h>
#include <vector>

class BGFXRenderer;
class UIDocument;

namespace Rml
{
    class Context;
}

// Shows UIDocument components through RmlUi (the Ultralight replacement, growing behind
// ME_RMLUI until UI-5). Documents render into an offscreen premultiplied target that the
// renderer composites over the main camera (RenderView::UIComposite), so the editor's game
// view shows them too. Mouse, keyboard and synthesized text input feed the context, .rml and
// .rcss edits hot reload, and the RmlUi debugger toggles on F7 outside retail builds.
class RmlUiCore final
    : public Core<RmlUiCore>
    , public EventReceiver
{
public:
    explicit RmlUiCore( BGFXRenderer* InRenderer );
    ~RmlUiCore();

    virtual void Init() final;
    virtual void Update( const UpdateContext& inUpdateContext ) final;

    // Tears down the context and (for the last user) the Rml library and its GPU resources.
    // Engine::Shutdown calls it while the renderer is still alive; cores are never deleted.
    void ReleaseRml();

    virtual void OnEntityAdded( Entity& NewEntity ) final;
    virtual void OnEntityRemoved( Entity& InEntity ) final;

#if USING( ME_RMLUI )
    Rml::Context* GetContext() const { return m_context; }
    // True while the pointer is over (or dragging in) a document: game input should yield.
    bool WantsMouse() const;
#endif

private:
#if USING( ME_RMLUI )
    void EnsureContext( const Vector2& InSize );
    void EnsureRenderTarget( uint16_t InWidth, uint16_t InHeight );
    void LoadDocument( UIDocument& InDocument );
    void CloseDocument( UIDocument& InDocument );
    void ProcessInput();
    void ApplyRequestedCursor();
    void HotReload();

    BGFXRenderer* m_renderer = nullptr;
    Rml::Context* m_context = nullptr;
    bool m_rmlInitialized = false;
    Vector2 m_contextSize;
    std::string m_contextName;

    bgfx::FrameBufferHandle m_frameBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_colorTexture = BGFX_INVALID_HANDLE;
    uint16_t m_targetWidth = 0;
    uint16_t m_targetHeight = 0;

    // Changed asset paths collected from AssetsChangedEvent, consumed in Update.
    std::vector<std::string> m_changedUIFiles;
    bool m_mouseWasDown[3] = {};
    std::string m_appliedCursor;
#endif
};
