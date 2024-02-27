#include "PCH.h"

#include "BasicUIView.h"
#include "imgui.h"

#if USING( ME_UI )
#include "UI/JSHelpers.h"
#include "Ultralight/View.h"
#include <Ultralight/String.h>
#endif

#include "Events/AudioEvents.h"
#include "Events/HavanaEvents.h"
#include "Events/SceneEvents.h"
#include "Core/Assert.h"
#include "Engine/Engine.h"

BasicUIView::BasicUIView()
    : Component( "BasicUIView" )
{

}

BasicUIView::BasicUIView( const char* Name )
    : Component( Name )
{
}

void BasicUIView::Init()
{
    SourceFile = File( FilePath );
}

void BasicUIView::OnSerialize( json& outJson )
{
    outJson["FilePath"] = FilePath.GetLocalPath();
}

void BasicUIView::OnDeserialize( const json& inJson )
{
    if( inJson.contains( "FilePath" ) )
    {
        FilePath = Path( inJson["FilePath"] );
    }
}

#if USING( ME_UI )

void BasicUIView::OnUpdateHistory( ultralight::View* caller )
{
}

void BasicUIView::OnDOMReady( ultralight::View* caller,
    uint64_t frame_id,
    bool is_main_frame,
    const ultralight::String& url )
{
    ultralight::RefPtr<ultralight::JSContext> context = caller->LockJSContext();
    ultralight::SetJSContext( context->ctx() );
    ultralight::JSObject GlobalWindow = ultralight::JSGlobalObject();

    GlobalWindow["PlaySound"] = BindJSCallback( &BasicUIView::PlaySound );
    GlobalWindow["LoadScene"] = BindJSCallback( &BasicUIView::LoadScene );
    GlobalWindow["Quit"] = BindJSCallback( &BasicUIView::Quit );

    OnUILoad( GlobalWindow, caller );
}

void BasicUIView::OnUILoad( ultralight::JSObject& GlobalWindow, ultralight::View* Caller )
{
}

#endif

void BasicUIView::ExecuteScript( const std::string& Script )
{
#if USING( ME_UI )
    ultralight::String excpt;
    ViewRef->EvaluateScript( Script.c_str(), &excpt );
    if( !excpt.empty() )
    {
        std::string exceptionString( excpt.utf8().data() );

        YIKES_FMT( "[UI] [%s]: %s", FilePath.GetLocalPathString().c_str(), exceptionString.c_str() );
    }
#endif
}

#if USING( ME_UI )

void BasicUIView::PlaySound( const ultralight::JSObject& thisObject, const ultralight::JSArgs& args )
{
    if( args.empty() || !args[0].IsString() )
    {
        BRUH( "PlaySound(string) argument mismatch." );
        return;
    }
    ultralight::String str = args[0].ToString();
    std::string str2( str.utf8().data() );
    PlayAudioEvent evt( str2 );
    evt.Callback = m_playAudioCallback;
    evt.Fire();
}

void BasicUIView::LoadScene( const ultralight::JSObject& thisObject, const ultralight::JSArgs& args )
{
    SharedPtr<LoadSceneEvent> evt = MakeShared<LoadSceneEvent>();
    ultralight::String path = args[0].ToString();
    Path requestedPath( std::string( path.utf8().data() ) );
    ME_ASSERT_MSG( requestedPath.Exists, "Level does not exist" );
    evt->Level = requestedPath.GetLocalPath();
    EventManager::GetInstance().QueueEvent( evt );
}

void BasicUIView::Quit( const ultralight::JSObject& thisObject, const ultralight::JSArgs& args )
{
    GetEngine().Quit();
}
#endif

#if USING( ME_EDITOR )

void BasicUIView::OnEditorInspect()
{
    ImVec2 selectorSize( -1.f, 19.f );
    HavanaUtils::Label( "Source" );
    if( ImGui::Button( ( ( !FilePath.GetLocalPath().empty() ) ? FilePath.GetLocalPath().data() : "Select Asset" ), selectorSize ) )
    {
        RequestAssetSelectionEvent evt( [this]( Path selectedAsset ) {
            FilePath = selectedAsset;
            SourceFile = File( FilePath );
            IsInitialized = false;
            }, AssetType::UI );
        evt.Fire();
    }
}

#endif
