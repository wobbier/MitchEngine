#pragma once
#include "ECS/Core.h"
#include "Dementia.h"
#include "Events/EventReceiver.h"
#include "Math/Vector2.h"

#include <bgfx/bgfx.h>
#include <deque>
#include <map>
#include <string>
#include <vector>

class BGFXRenderer;
class UIDocument;

namespace Rml
{
    class Context;
    class ElementDocument;
}

// Shows UIDocument components through RmlUi, the engine's game UI. Documents render into an
// offscreen premultiplied target (RenderView::RmlUi, in the UI band before the cameras) that the
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
    // Documents by their entity's name (the show-document / toggle-document actions, C#
    // UI.Show): false when no UIDocument entity has that name. Visibility applies next update.
    bool SetDocumentVisible( const std::string& InEntityName, bool InVisible );
    bool ToggleDocument( const std::string& InEntityName );
    bool IsDocumentVisible( const std::string& InEntityName );
    // The hide-document action: hides the UIDocument showing InDocument (an Rml::ElementDocument).
    void HideDocument( const void* InDocument );

    // The scene's loaded documents (UI-6, the editor's UI designer): entity name, .rml path.
    struct LoadedDocument
    {
        std::string EntityName;
        std::string Path;
        Rml::ElementDocument* Document = nullptr;
    };
    std::vector<LoadedDocument> GetLoadedDocuments();

    // Engine-owned documents with no entity (loading screens, splashes): they stay up while the
    // scene changes under them. Shown from the next update; InSortOrder stacks them like a
    // UIDocument's SortOrder (above the scene's documents by default).
    void ShowOverlay( const std::string& InPath, int InSortOrder = 1000 );
    void HideOverlay( const std::string& InPath );
    bool IsOverlayShown( const std::string& InPath ) const;

    Rml::Context* GetContext() const { return m_context; }
    // True while the pointer is over (or dragging in) a document: game input should yield.
    bool WantsMouse() const;

    // Script-owned view models (UI-3): values live here (not in C#), so they survive hot
    // reloads; the Rml data model binds lazily in Update and documents referencing a model
    // reload once when it first materializes. All calls are safe before the context exists.
    void ModelEnsure( const std::string& InModel );
    void ModelSetNumber( const std::string& InModel, const std::string& InKey, double InValue );
    void ModelSetString( const std::string& InModel, const std::string& InKey, const std::string& InValue );
    void ModelRegisterEvent( const std::string& InModel, const std::string& InKey );
    double ModelGetNumber( const std::string& InModel, const std::string& InKey ) const;
    std::string ModelGetString( const std::string& InModel, const std::string& InKey ) const;
    // Pops the oldest queued data-event name ("" when none are waiting).
    std::string ModelPollEvent( const std::string& InModel );

    // UI-4: dp-unit text scale (the context's density-independent pixel ratio), the string
    // table language ("" = none; loads Assets/UI/Strings/<code>.json and reloads documents),
    // and key injection for gamepad navigation and the input-press automation command.
    void SetTextScale( float InScale );
    float GetTextScale() const { return m_textScale; }
    void SetLanguage( const std::string& InCode );
    const std::string& GetLanguage() const { return m_language; }
    void InjectUiKey( int InKeyIdentifier, int InModifiers = 0 );
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
    void SyncModels();

    // One script view-model value: a number or a UTF-8 string.
    struct ModelValue
    {
        double Number = 0.0;
        std::string String;
        bool IsString = false;
        bool Bound = false;
        bool Dirty = false;
    };
    struct Model
    {
        std::map<std::string, ModelValue> Values;
        std::map<std::string, bool> Events;   // name -> bound to Rml yet
        std::deque<std::string> EventQueue;
        bool Created = false;
    };
    std::map<std::string, Model> m_models;
    struct Overlay
    {
        std::string Path;
        int SortOrder = 1000;
        Rml::ElementDocument* Document = nullptr;
        bool Failed = false;   // logged once, not retried every frame
    };
    std::vector<Overlay> m_overlays;
    void LoadOverlays();
    void CloseOverlays();
    bool m_modelsMaterialized = false;   // a model/variable appeared: reload loaded documents once
    float m_textScale = 1.f;
    std::string m_language;
    bool m_gamepadNavDown[5] = {};   // dpad + south, for press transitions

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
