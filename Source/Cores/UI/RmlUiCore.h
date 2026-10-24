#pragma once
#include "ECS/Core.h"
#include "Dementia.h"
#include "Math/Vector2.h"

class BGFXRenderer;
class UIDocument;

namespace Rml
{
    class Context;
}

// Shows UIDocument components through RmlUi (the Ultralight replacement, growing behind
// ME_RMLUI until UI-5). The spike renders straight to the backbuffer in game builds; the
// editor keeps documents loaded but draws nothing until the UI-2 composite path.
class RmlUiCore final
    : public Core<RmlUiCore>
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

private:
#if USING( ME_RMLUI )
    void EnsureContext( const Vector2& InSize );
    void LoadDocument( UIDocument& InDocument );
    void CloseDocument( UIDocument& InDocument );

    BGFXRenderer* m_renderer = nullptr;
    Rml::Context* m_context = nullptr;
    bool m_rmlInitialized = false;
    Vector2 m_contextSize;
    std::string m_contextName;
#endif
};
