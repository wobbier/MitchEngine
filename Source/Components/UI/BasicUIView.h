#pragma once
#include "ECS/Core.h"
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "File.h"
#include "Dementia.h"

#if USING( ME_UI )
#include "Ultralight/Listener.h"
#include "UI/JSHelpers.h"
#include "Ultralight/View.h"
#endif

#include "Components/Audio/AudioSource.h"

class BasicUIView
    : public Component<BasicUIView>
#if USING( ME_UI )
    , public ultralight::LoadListener
#endif
{
    friend class UICore;
public:
    BasicUIView();
    BasicUIView( const char* Name );

    virtual void Init() override;


#if USING( ME_EDITOR )
    virtual void OnEditorInspect() override;
#endif

#if USING( ME_UI )
    virtual void OnUpdateHistory( ultralight::View* caller ) override;

    virtual void OnDOMReady( ultralight::View* caller,
        uint64_t frame_id,
        bool is_main_frame,
        const ultralight::String& url ) final;

    virtual void OnUILoad( ultralight::JSObject& GlobalWindow, ultralight::View* Caller );
#endif

    void ExecuteScript( const std::string& Script );

    virtual void OnUpdate()
    {
    }

    Path FilePath;

protected:
    bool IsInitialized = false;
    size_t Index;
    File SourceFile;
    std::function<void( SharedPtr<AudioSource> )> m_playAudioCallback = nullptr;

#if USING( ME_UI )
    void PlaySound( const ultralight::JSObject& thisObject, const ultralight::JSArgs& args );
    void LoadScene( const ultralight::JSObject& thisObject, const ultralight::JSArgs& args );
    void Quit( const ultralight::JSObject& thisObject, const ultralight::JSArgs& args );
#endif

private:
    virtual void OnSerialize( json& outJson ) override;
    virtual void OnDeserialize( const json& inJson ) override;

#if USING( ME_UI )
    ultralight::RefPtr<ultralight::View> ViewRef;
#endif
};
ME_REGISTER_COMPONENT_FOLDER( BasicUIView, "UI" )
