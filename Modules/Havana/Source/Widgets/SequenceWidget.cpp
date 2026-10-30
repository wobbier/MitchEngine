#include "SequenceWidget.h"
#if USING( ME_EDITOR )
#include "Animation/SequenceAsset.h"
#include "Editor/SequenceDocument.h"
#include "Cores/SequenceCore.h"
#include "Components/Scripting/ScriptComponent.h"
#include "Components/Camera.h"
#include "Components/Animation/Animator.h"
#include "Components/Audio/AudioSource.h"
#include "Editor/EditorActions.h"
#include "Editor/EditorOperations.h"
#include "Editor/Selection.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Events/EditorEvents.h"
#include "Graphics/Texture.h"
#include "Resource/ResourceCache.h"
#include "Utils/ImGuiUtils.h"
#include <ImGuiTheme.h>
#include <filesystem>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <sstream>
#include <utility>

namespace
{
    SequenceWidget* s_instance = nullptr;

    void TargetPicker( const char* label, EntityHandle& target, World& world )
    {
        if( !ImGui::BeginCombo( label, target ? target->GetName().c_str() : "Unbound" ) ) return;
        if( ImGui::Selectable( "<Unbound>", !target ) ) target.Reset();
        world.ForEachEntity( [&]( Entity& entity ) {
            ImGui::PushID( static_cast<int>( entity.GetId().Index ) );
            if( ImGui::Selectable( entity.GetName().c_str(), entity.GetHandle() == target ) ) target = entity.GetHandle();
            ImGui::PopID();
        } );
        ImGui::EndCombo();
    }
}


SequenceWidget::SequenceWidget() : HavanaWidget( "Sequencer" )
{
    s_instance = this;
    IsOpen = false;
    Listen<OpenSequenceEvent>( [this]( const OpenSequenceEvent& event ) {
        if( event.PlayerGUID ) { SetOwner( EditorOps::GetWorld().FindEntityByGUID( event.PlayerGUID ) ); IsOpen = m_focusRequested = true; }
        else OpenAsset( event.ClipPath );
        return true;
    } );
}


SequenceWidget::~SequenceWidget() { if( s_instance == this ) s_instance = nullptr; }


SequenceWidget* SequenceWidget::Get() { return s_instance; }


Sequence* SequenceWidget::Working() const
{
    if( m_owner ) return m_owner->TryGetComponent<Sequence>();
    return m_standalone.get();
}


bool SequenceWidget::IsAsset() const
{
    const Sequence* sequence = Working();
    return m_document && sequence && sequence->Clip == m_document->Path;
}


bool SequenceWidget::HasUnsaved() const
{
    return m_document && ( m_document->IsDirty() || ( !m_editBefore.is_null() && m_editBefore != m_document->Data.ToJson() ) );
}


void SequenceWidget::Init()
{
    for( const char* icon : { "Play", "Pause", "Stop" } )
        m_icons[icon] = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( std::string( "Assets/Havana/UI/" ) + icon + ".png" ) );
    auto& actions = EditorActions::Get();
    actions.Register( { "Sequence.PreviewSelected", "Preview Selected Sequence", "Animation", 0, 0, [this]() {
        const auto entity = Selection::Get().GetActive();
        if( entity && entity->HasComponent<Sequence>() ) { SetOwner( entity ); IsOpen = true; entity->GetComponent<Sequence>().StartPreview( 0.f, true ); }
    } } );
    actions.Register( { "Sequence.StopPreviews", "Stop Sequence Previews", "Animation", 0, 0, []() {
        if( GetEngine().Sequences ) GetEngine().Sequences->StopAllPreviews();
    } } );
    actions.Register( { "Sequence.PreviewMiddle", "Preview Sequence Midpoint", "Animation", 0, 0, [this]() {
        const auto entity = Selection::Get().GetActive();
        if( entity && entity->HasComponent<Sequence>() ) { SetOwner( entity ); IsOpen = true; auto& clip = entity->GetComponent<Sequence>(); clip.StartPreview( clip.DurationFrames * 0.5f ); }
    } } );
    actions.Register( { "Sequence.SaveAsset", "Save Sequence Asset", "Animation", 0, 0, [this]() { if( IsAsset() ) { FinishEdit(); m_document->Save(); } else ExportDialog(); } } );
    actions.Register( { "Sequence.ExportAsset", "Save Sequence As Asset...", "Animation", 0, 0, [this]() { ExportDialog(); } } );
    actions.Register( { "Sequence.OpenAsset", "Open Sequence Asset...", "Animation", 0, 0, [this]() {
        RequestAssetSelectionEvent event( [this]( Path selected ) { OpenAsset( selected.GetLocalPathString() ); }, AssetType::Sequence ); event.Fire();
    } } );
    for( const auto& item : { std::pair{ "Sequence.NudgeLeft", ImGuiKey_LeftArrow }, std::pair{ "Sequence.NudgeRight", ImGuiKey_RightArrow } } )
    {
        const int delta = item.second == ImGuiKey_LeftArrow ? -1 : 1;
        actions.Register( { item.first, delta < 0 ? "Move Selected Keys Left" : "Move Selected Keys Right", "Animation", item.second, 0, [this, delta]() {
            if( Sequence* sequence = Working() ) { FinishEdit(); const json before = EditState(); m_timeline.Move( *sequence, delta ); CommitEdit( before ); }
        }, [this]() {
            const ImGuiWindow* focused = ImGui::GetCurrentContext()->NavWindow;
            const bool ownWindow = focused && focused->RootWindow && Name == focused->RootWindow->Name;
            return IsOpen && m_focused && ownWindow && !m_timeline.Selected.empty() && !( GetEngine().Sequences && GetEngine().Sequences->IsRunning() );
        } } );
    }
}


void SequenceWidget::InstallActionRouting()
{
    auto& actions = EditorActions::Get();
    auto claims = actions.DocumentClaims;
    auto execute = actions.DocumentExecute;
    actions.DocumentClaims = [this, claims]( const std::string& id ) { return ClaimsAction( id ) || ( claims && claims( id ) ); };
    actions.DocumentExecute = [this, claims, execute]( const std::string& id ) {
        if( ClaimsAction( id ) ) ExecuteAction( id );
        else if( claims && execute && claims( id ) ) execute( id );
    };
}


bool SequenceWidget::ClaimsAction( const std::string& id ) const
{
    if( !IsOpen || !m_focused || !Working() ) return false;
    if( GetEngine().Sequences && GetEngine().Sequences->IsRunning() ) return false;
    if( id == "Edit.Delete" || id == "Edit.SelectAll" || id == "Edit.Deselect" ) return true;
    if( id == "Edit.Copy" || id == "Edit.Cut" || id == "Edit.Paste" || id == "Edit.Duplicate" ) return true;
    return IsAsset() && ( id == "Edit.Undo" || id == "Edit.Redo" || id == "File.Save" || id == "File.SaveAs" );
}


void SequenceWidget::ExecuteAction( const std::string& id )
{
    FinishEdit();
    Sequence* sequence = Working();
    if( !sequence ) return;
    if( id == "File.Save" ) m_document->Save();
    else if( id == "File.SaveAs" ) ExportDialog();
    else if( id == "Edit.Undo" || id == "Edit.Redo" )
    {
        sequence->StopPreview();
        if( id == "Edit.Undo" ) m_document->History.Undo(); else m_document->History.Redo();
        m_document->Data.Apply( *sequence );
        m_timeline.Reset();
    }
    else if( id == "Edit.SelectAll" ) m_timeline.SelectBox( *sequence, 0.f, static_cast<float>( sequence->DurationFrames ), 0, static_cast<int>( sequence->Tracks.size() ) );
    else if( id == "Edit.Deselect" ) { m_timeline.Selected.clear(); m_timeline.SelectedMarker = m_timeline.SelectedCut = -1; }
    else if( id == "Edit.Copy" ) m_timeline.Copy( *sequence );
    else if( id == "Edit.Cut" || id == "Edit.Paste" || id == "Edit.Duplicate" )
    {
        // Paste lands at the playhead; Cut copies then deletes, as one undo step.
        const json before = EditState();
        if( id == "Edit.Paste" ) m_timeline.Paste( *sequence, m_frame );
        else if( id == "Edit.Duplicate" ) m_timeline.Duplicate( *sequence );
        else if( m_timeline.Copy( *sequence ) ) m_timeline.Delete( *sequence );
        CommitEdit( before );
    }
    else if( id == "Edit.Delete" )
    {
        const json before = EditState();
        if( !m_timeline.Selected.empty() ) m_timeline.Delete( *sequence );
        else if( m_timeline.SelectedMarker >= 0 ) m_timeline.DeleteMarker( *sequence );
        else m_timeline.DeleteCut( *sequence );
        CommitEdit( before );
    }
}


json SequenceWidget::EditState() const
{
    if( !Working() ) return json();
    return IsAsset() ? SequenceClipData::Capture( *Working() ).ToJson() : ( m_owner ? EditorOps::CaptureComponent( *m_owner.Get(), "Sequence" ) : json() );
}


void SequenceWidget::CommitEdit( const json& before )
{
    if( before.is_null() || !Working() ) return;
    if( IsAsset() ) { m_document->Data = SequenceClipData::Capture( *Working() ); m_document->Commit( before ); }
    else if( m_owner ) EditorOps::RecordComponentEdit( *m_owner.Get(), "Sequence", before, EditState(), "Edit Sequence" );
}


void SequenceWidget::FinishEdit()
{
    if( !m_editBefore.is_null() ) CommitEdit( m_editBefore );
    m_editBefore = json();
}


void SequenceWidget::ConfirmUnsaved( std::function<void()> action )
{
    FinishEdit();
    if( !HasUnsaved() ) { action(); return; }
    if( m_pending ) return;
    m_pending = std::move( action );
    m_openUnsaved = true;
    IsOpen = true;
}


void SequenceWidget::SetOwner( EntityHandle owner )
{
    if( owner == m_owner ) return;
    Sequence* next = owner ? owner->TryGetComponent<Sequence>() : nullptr;
    if( HasUnsaved() && ( !next || next->Clip != m_document->Path ) )
    {
        ConfirmUnsaved( [this, owner]() { SetOwner( owner ); } );
        return;
    }
    FinishEdit();
    if( Working() ) Working()->StopPreview();
    m_owner = owner;
    m_target = owner;
    m_track = -1;
    m_frame = 0;
    m_timeline.Reset();
    if( next && !next->Clip.empty() )
    {
        if( !m_document || m_document->Path != next->Clip )
        {
            auto document = std::make_unique<SequenceDocument>();
            if( document->Open( next->Clip ) ) m_document = std::move( document );
            else { m_error = document->Error; m_document.reset(); }
        }
        if( m_document ) m_document->Data.Apply( *next );
    }
    else m_document.reset();
}


bool SequenceWidget::OpenAsset( const std::string& path )
{
    if( HasUnsaved() && Path( path ).GetLocalPathString() != m_document->Path )
    {
        ConfirmUnsaved( [this, path]() { OpenAsset( path ); } );
        return true;
    }
    FinishEdit();
    if( Working() ) Working()->StopPreview();
    if( !m_document || m_document->Path != Path( path ).GetLocalPathString() )
    {
        auto document = std::make_unique<SequenceDocument>();
        if( !document->Open( path ) ) { m_error = document->Error; return false; }
        m_document = std::move( document );
    }
    Sequence* current = m_owner ? m_owner->TryGetComponent<Sequence>() : nullptr;
    if( !current || current->Clip != m_document->Path )
    {
        m_owner.Reset();
        m_standalone = std::make_unique<Sequence>();
        m_standalone->Clip = m_document->Path;
    }
    m_document->Data.Apply( *Working() );
    m_selectionVersion = Selection::Get().GetVersion();
    IsOpen = m_focusRequested = true;
    m_track = 0;
    m_frame = 0;
    m_timeline.Reset();
    return true;
}


bool SequenceWidget::ExportAsset( const std::string& path )
{
    FinishEdit();
    if( !Working() ) { m_error = "Choose a sequence to export"; return false; }
    const json before = m_owner ? EditorOps::CaptureComponent( *m_owner.Get(), "Sequence" ) : json();
    if( !Working()->ExportClip( path, m_error ) ) return false;
    if( m_owner ) EditorOps::RecordComponentEdit( *m_owner.Get(), "Sequence", before, EditorOps::CaptureComponent( *m_owner.Get(), "Sequence" ), "Create Sequence Asset" );
    // Save As preserves the live draft in a new document, even when the previous asset was dirty.
    m_document.reset();
    return OpenAsset( path );
}


void SequenceWidget::ExportDialog()
{
    RequestAssetSelectionEvent event( [this]( Path path ) { ExportAsset( path.GetLocalPathString() ); }, AssetType::Sequence, true );
    event.Fire();
}


void SequenceWidget::Destroy()
{
    FinishEdit();
    if( Working() ) Working()->StopPreview();
    m_document.reset();
    m_standalone.reset();
    m_owner.Reset();
    m_icons.clear();
}


void SequenceWidget::DrawUnsaved()
{
    if( m_openUnsaved ) { ImGui::OpenPopup( "Unsaved Sequence Asset" ); m_openUnsaved = false; }
    std::function<void()> run;
    if( ImGui::BeginPopupModal( "Unsaved Sequence Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
    {
        ImGui::TextWrapped( "Save changes to %s?", m_document ? m_document->Path.c_str() : "the sequence" );
        if( ImGui::Button( "Save" ) && m_document && m_document->Save() ) { run.swap( m_pending ); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if( ImGui::Button( "Discard" ) )
        {
            if( m_document ) { m_document->Open( m_document->Path ); if( Working() ) m_document->Data.Apply( *Working() ); }
            run.swap( m_pending ); ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if( ImGui::Button( "Cancel" ) ) { m_pending = nullptr; ImGui::CloseCurrentPopup(); }
        if( m_document && !m_document->Error.empty() ) ImGui::TextWrapped( "%s", m_document->Error.c_str() );
        ImGui::EndPopup();
    }
    if( run ) run();
}


void SequenceWidget::Render()
{
    if( !IsOpen ) { if( Working() ) Working()->StopPreview(); m_focused = false; return; }
    ImGui::SetNextWindowSize( ImVec2( 1220.f, 720.f ), ImGuiCond_FirstUseEver );
    if( m_focusRequested ) { ImGui::SetNextWindowFocus(); m_focusRequested = false; }
    ImGui::SetNextWindowSizeConstraints( ImVec2( ImGuiTheme::Em( 38.f ), ImGuiTheme::Em( 22.f ) ), ImVec2( FLT_MAX, FLT_MAX ) );
    const bool visible = ImGui::Begin( Name.c_str(), &IsOpen, ImGuiWindowFlags_NoScrollbar );
    const ImGuiWindow* focused = ImGui::GetCurrentContext()->NavWindow;
    const bool transient = focused && ( focused->Flags & ( ImGuiWindowFlags_Popup | ImGuiWindowFlags_MenuBar ) );
    if( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) ) m_focused = true;
    else if( !transient ) m_focused = false;
    if( visible ) DrawBody(); else if( Working() ) Working()->StopPreview();
    if( !IsOpen && HasUnsaved() ) { IsOpen = true; ConfirmUnsaved( [this]() { IsOpen = false; } ); }
    DrawUnsaved();
    ImGui::End();
}


bool SequenceWidget::TransportButton( const char* icon, const char* tooltip )
{
    const auto texture = m_icons.find( icon );
    bool pressed;
    if( texture != m_icons.end() && texture->second && bgfx::isValid( texture->second->TexHandle ) )
    {
        const float size = ImGui::GetFontSize();
        pressed = ImGui::ImageButton( icon, ImGui::toId( texture->second->TexHandle, IMGUI_FLAGS_ALPHA_BLEND, 0 ), ImVec2( size, size ),
            ImVec2( 0, 0 ), ImVec2( 1, 1 ), ImVec4(), ImGui::GetStyleColorVec4( ImGuiCol_Text ) );
    }
    else pressed = ImGui::Button( icon );
    ImGui::SetItemTooltip( "%s", tooltip );
    return pressed;
}


void SequenceWidget::AssignAsset( const std::string& path )
{
    const EntityHandle owner = m_owner;
    if( !owner ) return;
    ConfirmUnsaved( [this, owner, path]() {
        if( !owner || !owner->HasComponent<Sequence>() ) return;
        const json before = EditorOps::CaptureComponent( *owner.Get(), "Sequence" );
        auto& player = owner->GetComponent<Sequence>();
        player.Clip = path;
        player.RefreshClip( true );
        if( !player.Tracks.empty() && !player.Tracks[0].Target ) player.SetBinding( player.Tracks[0].Binding, owner );
        EditorOps::RecordComponentEdit( *owner.Get(), "Sequence", before, EditorOps::CaptureComponent( *owner.Get(), "Sequence" ), "Assign Sequence Clip" );
        m_document.reset(); m_owner.Reset(); SetOwner( owner );
    } );
}


void SequenceWidget::DrawBody()
{
    World& world = EditorOps::GetWorld();
    EntityHandle selected = Selection::Get().GetActive();
    if( m_selectionVersion != Selection::Get().GetVersion() )
    {
        m_selectionVersion = Selection::Get().GetVersion();
        if( selected && selected->HasComponent<Sequence>() ) SetOwner( selected );
    }
    Sequence* sequence = Working();
    if( !sequence && m_document )
    {
        m_owner.Reset();
        m_standalone = std::make_unique<Sequence>();
        m_standalone->Clip = m_document->Path;
        m_document->Data.Apply( *m_standalone );
        sequence = m_standalone.get();
    }
    const bool runtime = GetEngine().Sequences && GetEngine().Sequences->IsRunning();
    if( !runtime && IsAsset() && SequenceClipData::Capture( *sequence ).ToJson() != m_document->Data.ToJson() ) m_document->Data.Apply( *sequence );

    // File operations stay in one menu, leaving the header for document identity and state.
    ImGui::BeginDisabled( runtime );
    if( ImGui::Button( "Clip..." ) ) ImGui::OpenPopup( "Clip actions" );
    if( ImGui::BeginPopup( "Clip actions" ) )
    {
        if( ImGui::MenuItem( "Open asset..." ) )
        {
            RequestAssetSelectionEvent event( [this]( Path path ) { OpenAsset( path.GetLocalPathString() ); }, AssetType::Sequence ); event.Fire();
        }
        if( ImGui::MenuItem( "Save clip", "Ctrl+S", false, IsAsset() ) ) { FinishEdit(); m_document->Save(); }
        if( ImGui::MenuItem( "Save as asset...", nullptr, false, sequence != nullptr ) ) ExportDialog();
        if( ImGui::MenuItem( "Reload from disk", nullptr, false, IsAsset() ) )
            ConfirmUnsaved( [this]() { m_document->Open( m_document->Path ); if( Working() ) m_document->Data.Apply( *Working() ); m_timeline.Reset(); } );
        ImGui::Separator();
        if( ImGui::MenuItem( "Assign asset to player...", nullptr, false, bool( m_owner ) ) )
        {
            RequestAssetSelectionEvent event( [this]( Path path ) { AssignAsset( path.GetLocalPathString() ); }, AssetType::Sequence ); event.Fire();
        }
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if( sequence )
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( sequence->Name.c_str() );
        ImGui::SameLine();
        ImGui::TextColored( HasUnsaved() ? ImGuiTheme::WarningText() : ImGuiTheme::AccentText(), "%s", runtime ? "PLAY MODE" : IsAsset() ? ( HasUnsaved() ? "MODIFIED" : "ASSET" ) : "EMBEDDED" );
        const std::string path = runtime ? sequence->GetActiveClipPath() : IsAsset() ? m_document->Path : "Save as an asset to reuse this animation";
        ImGui::TextDisabled( "%s", path.c_str() );
    }
    else ImGui::TextDisabled( "No animation open" );
    ImGui::Separator();
    if( !sequence )
    {
        ImGuiTheme::SectionGap();
        ImGui::TextUnformatted( "Build a sequence" );
        ImGui::TextWrapped( "Open an animation asset, or add a Sequence player to a game object to begin." );
        ImGui::BeginDisabled( !selected );
        if( ImGui::Button( "Add Sequence to Selected" ) ) { EditorOps::AddComponent( *selected.Get(), "Sequence" ); SetOwner( selected ); }
        ImGui::EndDisabled();
        return;
    }

    // A compact transport is always visible above the timeline and inspector.
    ImGui::BeginDisabled( !m_owner );
    const bool playing = sequence->IsPlaying();
    if( TransportButton( playing ? "Pause" : "Play", playing ? "Pause playback" : "Play from the current frame" ) )
    {
        if( playing ) sequence->Pause();
        else if( runtime ) sequence->Play( false );
        else { if( m_frame >= sequence->DurationFrames ) m_frame = 0; sequence->StartPreview( static_cast<float>( m_frame ), true ); }
    }
    ImGui::SameLine();
    if( TransportButton( "Stop", runtime ? "Stop and rewind" : "Stop preview and restore the scene" ) )
    {
        if( runtime ) sequence->Stop(); else { sequence->StopPreview(); m_frame = 0; }
    }
    ImGui::SameLine();
    if( runtime || sequence->IsPreviewing() ) m_frame = static_cast<int>( std::lround( sequence->GetFrame() ) );
    ImGui::SetNextItemWidth( ImGuiTheme::Em( 4.5f ) );
    if( ImGui::DragInt( "##Playhead", &m_frame, 1.f, 0, sequence->DurationFrames, "%d f", ImGuiSliderFlags_AlwaysClamp ) )
    {
        if( runtime ) sequence->Seek( float( m_frame ) / sequence->FrameRate ); else sequence->StartPreview( static_cast<float>( m_frame ) );
    }
    ImGui::SetItemTooltip( "Playhead frame. Drag to scrub." );
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled( "/ %d   %.2f / %.2f s", sequence->DurationFrames, float( m_frame ) / sequence->FrameRate, sequence->GetDuration() );
    ImGui::SameLine();
    ImGui::BeginDisabled( runtime );
    ImGui::SetNextItemWidth( std::max( ImGuiTheme::Em( 7.f ), std::min( ImGuiTheme::Em( 16.f ), ImGui::GetContentRegionAvail().x ) ) );
    if( ImGui::BeginCombo( "##Preview player", m_owner ? m_owner->GetName().c_str() : "Choose preview player" ) )
    {
        world.ForEachEntity( [&]( Entity& entity ) {
            if( !entity.HasComponent<Sequence>() ) return;
            ImGui::PushID( static_cast<int>( entity.GetId().Index ) );
            if( ImGui::Selectable( entity.GetName().c_str(), m_owner == entity.GetHandle() ) ) { SetOwner( entity.GetHandle() ); Selection::Get().Set( m_owner ); }
            ImGui::PopID();
        } );
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip( "Scene player used for preview and object bindings" );
    ImGui::EndDisabled();
    // Changing the player can replace the working sequence/document.
    if( sequence != Working() ) return;
    ImGui::Separator();
    if( !runtime ) sequence->TouchPreview();

    const bool asset = IsAsset();
    const json before = runtime ? json() : EditState();
    const json sceneBefore = !runtime && asset && m_owner ? EditorOps::CaptureComponent( *m_owner.Get(), "Sequence" ) : json();
    m_frame = std::clamp( m_frame, 0, sequence->DurationFrames );
    if( !sequence->Tracks.empty() && ( m_track < 0 || m_track >= static_cast<int>( sequence->Tracks.size() ) ) ) m_track = 0;
    const float statusHeight = ImGui::GetFrameHeightWithSpacing();
    const float paneHeight = std::max( ImGuiTheme::Em( 8.f ), ImGui::GetContentRegionAvail().y - statusHeight );
    if( ImGui::BeginTable( "Sequence workspace", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImVec2( 0.f, paneHeight ) ) )
    {
        ImGui::TableSetupColumn( "Timeline", ImGuiTableColumnFlags_WidthStretch, 1.f );
        ImGui::TableSetupColumn( "Inspector", ImGuiTableColumnFlags_WidthFixed, ImGuiTheme::Em( 20.f ) );
        ImGui::TableNextColumn();
        ImGui::BeginChild( "Timeline pane", ImVec2( 0.f, paneHeight ), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar );
        ImGui::BeginDisabled( runtime );
        // New cuts use the selected camera.
        m_timeline.CutCamera = selected && selected->HasComponent<Camera>() ? selected : EntityHandle();
        m_timeline.Draw( *sequence, m_frame, m_track );
        // A clip names cut cameras by slot.
        if( !runtime && IsAsset() && std::any_of( sequence->Cuts.begin(), sequence->Cuts.end(), []( const SequenceCut& cut ) { return cut.Binding.empty(); } ) )
            sequence->PrepareAssetBindings();
        ImGui::EndDisabled();
        ImGui::EndChild();
        ImGui::TableNextColumn();
        ImGui::BeginChild( "Sequence inspector", ImVec2( 0.f, paneHeight ), ImGuiChildFlags_AlwaysUseWindowPadding );
        ImGui::BeginDisabled( runtime );
        // Picking a marker on the canvas brings its tab forward.
        const bool showEvents = m_timeline.SelectedMarker >= 0 && m_timeline.SelectedMarker != m_shownMarker;
        m_shownMarker = m_timeline.SelectedMarker;
        const bool showCuts = m_timeline.SelectedCut >= 0 && m_timeline.SelectedCut != m_shownCut;
        m_shownCut = m_timeline.SelectedCut;
        if( ImGui::BeginTabBar( "Inspector tabs" ) )
        {
            if( ImGui::BeginTabItem( "Track" ) ) { DrawTrackInspector( *sequence, selected ); ImGui::EndTabItem(); }
            if( ImGui::BeginTabItem( "Events", nullptr, showEvents ? ImGuiTabItemFlags_SetSelected : 0 ) ) { DrawEventInspector( *sequence, selected ); ImGui::EndTabItem(); }
            if( ImGui::BeginTabItem( "Cameras", nullptr, showCuts ? ImGuiTabItemFlags_SetSelected : 0 ) ) { DrawCutInspector( *sequence, selected ); ImGui::EndTabItem(); }
            if( ImGui::BeginTabItem( "Player" ) ) { DrawPlayerInspector( *sequence, world ); ImGui::EndTabItem(); }
            if( ImGui::BeginTabItem( "Clips" ) ) { DrawClipLibrary( *sequence ); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        ImGui::EndDisabled();
        ImGui::EndChild();
        ImGui::EndTable();
    }
    // A clip switch can replace the document; it already records its own scene edit.
    if( sequence != Working() || asset != IsAsset() || ( asset && sequence->Clip != m_document->Path ) ) return;
    if( !runtime )
    {
        const json after = EditState();
        if( before != after && m_editBefore.is_null() ) m_editBefore = before;
        if( asset ) m_document->Data = SequenceClipData::Capture( *sequence );
        if( asset && m_owner ) EditorOps::RecordComponentEdit( *m_owner.Get(), "Sequence", sceneBefore, EditorOps::CaptureComponent( *m_owner.Get(), "Sequence" ), "Edit Sequence Player" );
        if( !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown( ImGuiMouseButton_Left ) ) FinishEdit();
    }
    if( !m_assignRequested.empty() )
    {
        const std::string path = std::exchange( m_assignRequested, {} );
        AssignAsset( path );
        return;
    }
    const std::string error = !m_error.empty() ? m_error : m_document && !m_document->Error.empty() ? m_document->Error : sequence->GetClipError();
    if( !error.empty() ) { ImGui::TextColored( ImGuiTheme::ErrorText(), "%s", error.c_str() ); ImGui::SetItemTooltip( "%s", error.c_str() ); }
    else ImGui::TextDisabled( "%zu tracks   |   %zu selected   |   %s", sequence->Tracks.size(), m_timeline.Selected.size(), runtime ? "Runtime playback" : m_owner ? "Preview restores the scene on stop" : "Bind a scene player to preview" );
}


void SequenceWidget::DrawTrackInspector( Sequence& sequence, EntityHandle selected )
{
    if( ImGui::Button( "+ Add Track" ) )
    {
        sequence.StopPreview();
        SequenceTrack lane; lane.Target = m_target ? m_target : m_owner; lane.Capture( 0 );
        sequence.Tracks.push_back( lane );
        if( IsAsset() ) sequence.PrepareAssetBindings();
        m_track = static_cast<int>( sequence.Tracks.size() ) - 1;
        m_timeline.Selected.clear();
    }
    if( m_track < 0 || m_track >= static_cast<int>( sequence.Tracks.size() ) )
    {
        ImGuiTheme::SectionGap();
        ImGui::TextWrapped( "Select a track to edit its property and keyframes." );
        return;
    }
    auto& lane = sequence.Tracks[m_track];
    ImGui::SameLine();
    const bool remove = ImGui::Button( "Remove" );
    ImGui::SeparatorText( "Property" );
    ImGui::Checkbox( "Track enabled", &lane.Enabled );
    if( ImGui::IsItemEdited() ) sequence.StopPreview();
    ImGui::SameLine();
    if( ImGui::Checkbox( "Locked", &lane.Locked ) && lane.Locked ) m_timeline.Selected.clear();
    ImGui::SetItemTooltip( "A locked track's keys can't be selected, moved, deleted or pasted over" );
    if( !lane.Binding.empty() ) ImGui::TextDisabled( "Slot: %s", lane.Binding.c_str() );
    if( m_owner )
    {
        EntityHandle target = lane.Target;
        ImGui::SetNextItemWidth( -1.f );
        TargetPicker( "##Track target", target, EditorOps::GetWorld() );
        if( target != lane.Target ) { sequence.StopPreview(); if( IsAsset() ) sequence.SetBinding( lane.Binding, target ); else lane.Target = target; }
        if( selected && ImGui::SmallButton( "Bind selected object" ) ) { if( IsAsset() ) sequence.SetBinding( lane.Binding, selected ); else lane.Target = selected; m_target = selected; }
    }
    ImGui::SetNextItemWidth( -1.f );
    if( lane.Target && ImGui::BeginCombo( "##Component", lane.Component.c_str() ) )
    {
        for( BaseComponent* component : lane.Target->GetAllComponents() )
        {
            const auto properties = SequenceTrack::GetProperties( *component );
            if( !properties.empty() && ImGui::Selectable( component->GetName().c_str(), component->GetName() == lane.Component ) )
            {
                sequence.StopPreview(); lane.Component = component->GetName(); lane.Property = properties.front(); lane.Keys.clear(); lane.Capture( m_frame ); m_timeline.Selected.clear();
            }
        }
        ImGui::EndCombo();
    }
    BaseComponent* component = lane.ResolveComponent();
    ImGui::SetNextItemWidth( -1.f );
    if( component && ImGui::BeginCombo( "##Property", lane.Property.c_str() ) )
    {
        for( const auto& property : SequenceTrack::GetProperties( *component ) )
            if( ImGui::Selectable( property.c_str(), property == lane.Property ) ) { sequence.StopPreview(); lane.Property = property; lane.Keys.clear(); lane.Capture( m_frame ); m_timeline.Selected.clear(); }
        ImGui::EndCombo();
    }
    if( !component ) ImGui::TextWrapped( "%s.%s: bind an object to capture values", lane.Component.c_str(), lane.Property.c_str() );
    const auto property = lane.ResolveProperty();
    ImGui::SeparatorText( "Keyframes" );
    ImGui::BeginDisabled( !property );
    if( ImGui::Button( "Key Current Value", ImVec2( -1.f, 0.f ) ) ) lane.Capture( m_frame );
    ImGui::EndDisabled();
    using P = Reflection::PropertyType;
    const auto type = property ? property.Ops->Type : P::Vector4;
    if( type == P::Quaternion ) ImGui::TextDisabled( "Rotation values in degrees" );
    if( lane.Keys.empty() ) ImGui::TextDisabled( "No keyframes yet" );
    if( ImGui::BeginTable( "Key values", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH, ImVec2( 0.f, std::max( ImGuiTheme::Em( 5.f ), ImGui::GetContentRegionAvail().y ) ) ) )
    {
        ImGui::TableSetupColumn( "Frame", ImGuiTableColumnFlags_WidthFixed, ImGuiTheme::Em( 3.5f ) );
        ImGui::TableSetupColumn( "Value / interpolation", ImGuiTableColumnFlags_WidthStretch );
        ImGui::TableSetupColumn( "##Remove", ImGuiTableColumnFlags_WidthFixed, ImGuiTheme::Em( 1.6f ) );
        ImGui::TableSetupScrollFreeze( 0, 1 );
        ImGui::TableHeadersRow();
        // Rows contain both values and interpolation, so each row has a consistent height.
        ImGuiListClipper clipper; clipper.Begin( static_cast<int>( lane.Keys.size() ), ImGui::GetFrameHeightWithSpacing() * 2.f + ImGui::GetStyle().CellPadding.y * 2.f );
        bool keyHandled = false;
        while( !keyHandled && clipper.Step() ) for( int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i )
        {
            ImGui::PushID( i );
            SequenceKey key = lane.Keys[i];
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::SetNextItemWidth( -1.f );
            bool changed = ImGui::DragInt( "##Frame", &key.Frame, 1.f, 0, sequence.DurationFrames, "%d", ImGuiSliderFlags_AlwaysClamp );
            ImGui::TableNextColumn(); ImGui::SetNextItemWidth( -1.f );
            if( type == P::Quaternion )
            {
                Vector3 euler = Quaternion::ToEulerAngles( Quaternion( key.Value.x, key.Value.y, key.Value.z, key.Value.w ) );
                if( ImGui::DragFloat3( "##Value", &euler.x, 0.25f, 0.f, 0.f, "%.1f" ) ) { const Quaternion q = Quaternion::FromEulerDegrees( euler ); key.Value = Vector4( q.x, q.y, q.z, q.w ); changed = true; }
            }
            else if( type == P::Float || type == P::Double ) changed |= ImGui::DragFloat( "##Value", &key.Value.x, 0.1f, 0.f, 0.f, "%.2f" );
            else if( type == P::Vector2 ) changed |= ImGui::DragFloat2( "##Value", &key.Value.x, 0.1f, 0.f, 0.f, "%.2f" );
            else if( type == P::Vector3 ) changed |= ImGui::DragFloat3( "##Value", &key.Value.x, 0.1f, 0.f, 0.f, "%.2f" );
            else changed |= ImGui::DragFloat4( "##Value", &key.Value.x, 0.1f, 0.f, 0.f, "%.2f" );
            ImGui::SetNextItemWidth( -1.f );
            int interpolation = static_cast<int>( key.Interpolation );
            const bool fov = lane.Component == "Camera" && lane.Property == "FieldOfView";
            if( ImGui::Combo( "##Interpolation", &interpolation, fov ? "Linear\0SmoothStep\0Hold\0Focal Length\0Smooth Focal Length\0" : "Linear\0SmoothStep\0Hold\0" ) ) { key.Interpolation = static_cast<SequenceInterpolation>( interpolation ); changed = true; }
            ImGui::TableNextColumn(); const bool erase = ImGui::SmallButton( "x" );
            ImGui::SetItemTooltip( "Delete key" );
            ImGui::PopID();
            if( changed || erase )
            {
                sequence.StopPreview();
                lane.Keys.erase( lane.Keys.begin() + i );
                if( !erase ) lane.SetKey( key );
                m_timeline.Selected.clear();
                keyHandled = true;
                break;
            }
        }
        ImGui::EndTable();
    }
    if( remove ) { sequence.StopPreview(); sequence.Tracks.erase( sequence.Tracks.begin() + m_track ); m_track = -1; m_timeline.Reset(); }
}


void SequenceWidget::SetMarkerReceiver( Sequence& sequence, SequenceMarker& marker, EntityHandle target )
{
    sequence.StopPreview();
    // The player itself needs no slot: every instance's own player receives it.
    if( !target || target == m_owner ) { marker.Binding.clear(); marker.Target.Reset(); return; }
    if( IsAsset() && !marker.Binding.empty() ) { sequence.SetBinding( marker.Binding, target ); return; }
    marker.Target = target;
    if( IsAsset() ) sequence.PrepareAssetBindings();
}


void SequenceWidget::DrawEventInspector( Sequence& sequence, EntityHandle selected )
{
    if( ImGui::Button( "+ Add Event at Playhead", ImVec2( -1.f, 0.f ) ) ) m_timeline.AddMarker( sequence, m_frame );
    if( sequence.Markers.empty() )
    {
        ImGuiTheme::SectionGap();
        ImGui::TextWrapped( "Events call scripts when playback passes them: a door opening on a beat, a line of dialogue, a camera shake. Double-click the Events lane to add one." );
    }
    if( ImGui::BeginListBox( "##Events", ImVec2( -1.f, std::min( ImGuiTheme::Em( 8.f ), ImGui::GetFrameHeightWithSpacing() * ( sequence.Markers.size() + 0.5f ) ) ) ) )
    {
        for( int i = 0; i < static_cast<int>( sequence.Markers.size() ); ++i )
        {
            ImGui::PushID( i );
            const std::string label = std::to_string( sequence.Markers[i].Frame ) + "  " + sequence.Markers[i].Name;
            if( ImGui::Selectable( label.c_str(), i == m_timeline.SelectedMarker ) ) { m_timeline.SelectedMarker = i; m_timeline.Selected.clear(); }
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }
    const int index = m_timeline.SelectedMarker;
    if( index < 0 || index >= static_cast<int>( sequence.Markers.size() ) ) return;
    SequenceMarker& marker = sequence.Markers[index];
    ImGui::SeparatorText( "Event" );
    ImGui::TextDisabled( "Name" ); ImGui::SetNextItemWidth( -1.f );
    if( ImGui::InputText( "##Event name", &marker.Name ) ) sequence.StopPreview();
    if( marker.Name.empty() ) ImGui::TextColored( ImGuiTheme::WarningText(), "Name the event so receivers can tell it apart." );
    int frame = marker.Frame;
    ImGui::SetNextItemWidth( ImGuiTheme::Em( 5.f ) );
    if( ImGui::DragInt( "Frame", &frame, 1.f, 0, sequence.DurationFrames, "%d f", ImGuiSliderFlags_AlwaysClamp ) ) { m_timeline.MoveMarker( sequence, index, frame ); return; }
    ImGui::TextDisabled( "Receiver" );
    EntityHandle target = marker.Target ? marker.Target : marker.Binding.empty() ? m_owner : EntityHandle();
    const EntityHandle old = target;
    ImGui::SetNextItemWidth( -1.f );
    if( m_owner ) TargetPicker( "##Receiver", target, EditorOps::GetWorld() );
    if( target != old ) SetMarkerReceiver( sequence, marker, target );
    if( m_owner && selected && selected != target && ImGui::SmallButton( "Use selected object" ) ) SetMarkerReceiver( sequence, marker, selected );
    if( !marker.Binding.empty() ) ImGui::TextDisabled( "Slot: %s", marker.Binding.c_str() );
    const EntityHandle receiver = marker.ResolveReceiver( m_owner );
    if( !marker.Binding.empty() && !marker.Target ) ImGui::TextColored( ImGuiTheme::WarningText(), "Slot %s isn't bound on this player: only EventBus subscribers hear it.", marker.Binding.c_str() );
    else if( receiver && !receiver->HasComponent<ScriptComponent>() ) ImGui::TextColored( ImGuiTheme::WarningText(), "%s has no script: only EventBus subscribers hear it.", receiver->GetName().c_str() );
    // Audio and animation cues act on the receiver directly; every marker still sends its event.
    int action = static_cast<int>( marker.Action );
    ImGui::SetNextItemWidth( -1.f );
    if( ImGui::Combo( "##Action", &action, "Event (scripts only)\0Play audio\0Stop audio\0Play animation state\0" ) ) marker.Action = static_cast<SequenceMarkerAction>( action );
    const bool audio = marker.Action == SequenceMarkerAction::PlayAudio || marker.Action == SequenceMarkerAction::StopAudio;
    const bool animation = marker.Action == SequenceMarkerAction::PlayAnimation;
    if( receiver && audio && !receiver->HasComponent<AudioSource>() ) ImGui::TextColored( ImGuiTheme::WarningText(), "%s has no AudioSource.", receiver->GetName().c_str() );
    if( receiver && animation && !receiver->HasComponent<Animator>() ) ImGui::TextColored( ImGuiTheme::WarningText(), "%s has no Animator.", receiver->GetName().c_str() );
    if( marker.Action == SequenceMarkerAction::StopAudio ) ImGui::TextDisabled( "Stops the receiver's AudioSource." );
    else if( animation && receiver && receiver->HasComponent<Animator>() && !receiver->GetComponent<Animator>().States.empty() )
    {
        ImGui::TextDisabled( "State" ); ImGui::SetNextItemWidth( -1.f );
        if( ImGui::BeginCombo( "##State", marker.Payload.empty() ? "Choose a state" : marker.Payload.c_str() ) )
        {
            for( const auto& state : receiver->GetComponent<Animator>().States )
                if( ImGui::Selectable( state.Name.c_str(), state.Name == marker.Payload ) ) marker.Payload = state.Name;
            ImGui::EndCombo();
        }
    }
    else
    {
        ImGui::TextDisabled( "%s", animation ? "State" : audio ? "Clip (empty: the source's own)" : "Payload" ); ImGui::SetNextItemWidth( -1.f );
        ImGui::InputText( "##Payload", &marker.Payload );
        ImGui::SetItemTooltip( "%s", animation ? "The Animator state to play" : audio ? "A clip path to play as a one-shot at the source" : "Text passed to the receiver (SequenceEvent.Payload)" );
    }
    if( animation && marker.Payload.empty() ) ImGui::TextColored( ImGuiTheme::WarningText(), "Name the state to play." );
    if( marker.Action != SequenceMarkerAction::StopAudio )
    {
        ImGui::SetNextItemWidth( ImGuiTheme::Em( 5.f ) );
        ImGui::DragFloat( animation ? "Fade (s)" : audio ? "Volume" : "Number", &marker.Number, animation ? 0.01f : 0.1f, 0.f, 0.f, "%.2f" );
        ImGui::SetItemTooltip( "%s", animation ? "Crossfade into the state, in seconds" : audio ? "One-shot volume scale (0 = 1)" : "A number passed to the receiver (SequenceEvent.Number)" );
    }
    if( ImGui::Button( "Delete Event" ) ) { m_timeline.DeleteMarker( sequence ); return; }
    ImGuiTheme::SectionGap();
    ImGui::TextDisabled( "C# (on the receiver)" );
    ImGui::TextUnformatted( "class Door : Script, ISequenceReceiver" );
    ImGui::TextUnformatted( "  public void OnSequenceEvent(SequenceEvent e)" );
    ImGui::TextUnformatted( "  { if (e.Name == \"Open\") ... }" );
}


void SequenceWidget::SetCutCamera( Sequence& sequence, SequenceCut& cut, EntityHandle camera )
{
    sequence.StopPreview();
    if( IsAsset() && !cut.Binding.empty() ) { sequence.SetBinding( cut.Binding, camera ); return; }
    cut.Target = camera;
    if( IsAsset() ) sequence.PrepareAssetBindings();
}


void SequenceWidget::DrawCutInspector( Sequence& sequence, EntityHandle selected )
{
    const bool selectedCamera = selected && selected->HasComponent<Camera>();
    if( ImGui::Button( selectedCamera ? "+ Cut to Selected Camera at Playhead" : "+ Add Cut at Playhead", ImVec2( -1.f, 0.f ) ) )
    {
        m_timeline.CutCamera = selectedCamera ? selected : EntityHandle();
        m_timeline.AddCut( sequence, m_frame );
        if( IsAsset() ) sequence.PrepareAssetBindings();
    }
    if( sequence.Cuts.empty() )
    {
        ImGuiTheme::SectionGap();
        ImGui::TextWrapped( "Camera cuts switch the Game View between cameras: each shot lasts until the next cut. Select a camera and double-click the Cameras lane to cut to it." );
        return;
    }
    if( ImGui::BeginListBox( "##Cuts", ImVec2( -1.f, std::min( ImGuiTheme::Em( 8.f ), ImGui::GetFrameHeightWithSpacing() * ( sequence.Cuts.size() + 0.5f ) ) ) ) )
    {
        for( int i = 0; i < static_cast<int>( sequence.Cuts.size() ); ++i )
        {
            ImGui::PushID( i );
            const SequenceCut& cut = sequence.Cuts[i];
            const std::string label = std::to_string( cut.Frame ) + "  " + ( cut.Target ? cut.Target->GetName() : cut.Binding.empty() ? std::string( "No camera" ) : cut.Binding );
            if( ImGui::Selectable( label.c_str(), i == m_timeline.SelectedCut ) ) { m_timeline.SelectedCut = i; m_timeline.SelectedMarker = -1; m_timeline.Selected.clear(); }
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }
    const int index = m_timeline.SelectedCut;
    if( index < 0 || index >= static_cast<int>( sequence.Cuts.size() ) ) return;
    SequenceCut& cut = sequence.Cuts[index];
    ImGui::SeparatorText( "Cut" );
    int frame = cut.Frame;
    ImGui::SetNextItemWidth( ImGuiTheme::Em( 5.f ) );
    if( ImGui::DragInt( "Frame", &frame, 1.f, 0, sequence.DurationFrames, "%d f", ImGuiSliderFlags_AlwaysClamp ) ) { m_timeline.MoveCut( sequence, index, frame ); return; }
    ImGui::TextDisabled( "Camera" );
    if( m_owner && ImGui::BeginCombo( "##Cut camera", cut.Target ? cut.Target->GetName().c_str() : "No camera" ) )
    {
        EditorOps::GetWorld().ForEachEntity( [&]( Entity& entity ) {
            if( !entity.HasComponent<Camera>() ) return;
            ImGui::PushID( static_cast<int>( entity.GetId().Index ) );
            if( ImGui::Selectable( entity.GetName().c_str(), entity.GetHandle() == cut.Target ) ) SetCutCamera( sequence, cut, entity.GetHandle() );
            ImGui::PopID();
        } );
        ImGui::EndCombo();
    }
    if( m_owner && selectedCamera && selected != cut.Target && ImGui::SmallButton( "Use selected camera" ) ) SetCutCamera( sequence, cut, selected );
    if( !cut.Binding.empty() ) ImGui::TextDisabled( "Slot: %s", cut.Binding.c_str() );
    if( !cut.Target ) ImGui::TextColored( ImGuiTheme::WarningText(), "%s", cut.Binding.empty() ? "Choose the camera this shot uses." : "This slot isn't bound on this player: the shot keeps the previous camera." );
    else if( !cut.Target->HasComponent<Camera>() ) ImGui::TextColored( ImGuiTheme::WarningText(), "%s has no Camera component.", cut.Target->GetName().c_str() );
    if( ImGui::Button( "Delete Cut" ) ) { m_timeline.DeleteCut( sequence ); return; }
    ImGuiTheme::SectionGap();
    ImGui::TextWrapped( "Before the first cut the scene's own camera stays current. A finished sequence holds its last shot until gameplay changes cameras." );
}


void SequenceWidget::DrawPlayerInspector( Sequence& sequence, World& world )
{
    ImGui::SeparatorText( "Clip settings" );
    ImGui::TextDisabled( "Name" ); ImGui::SetNextItemWidth( -1.f );
    bool settings = ImGui::InputText( "##Name", &sequence.Name );
    ImGui::SetNextItemWidth( ImGuiTheme::Em( 5.f ) );
    settings |= ImGui::DragInt( "Frame rate", &sequence.FrameRate, 1.f, 1, 240, "%d fps", ImGuiSliderFlags_AlwaysClamp );
    int lastKey = 1;
    for( const auto& lane : sequence.Tracks ) for( const auto& key : lane.Keys ) lastKey = std::max( lastKey, key.Frame );
    ImGui::SetNextItemWidth( ImGuiTheme::Em( 5.f ) );
    settings |= ImGui::DragInt( "Length", &sequence.DurationFrames, 1.f, lastKey, 100000, "%d f", ImGuiSliderFlags_AlwaysClamp );
    if( settings ) sequence.StopPreview();
    ImGui::TextDisabled( "%.2f seconds", sequence.GetDuration() );
    if( !m_owner ) { ImGui::TextWrapped( "Choose a scene player to configure playback and object bindings." ); return; }
    ImGui::SeparatorText( "Playback" );
    ImGui::Checkbox( "Play on start", &sequence.PlayOnStart );
    ImGui::Checkbox( "Loop", &sequence.Loop );
    ImGui::SetNextItemWidth( ImGuiTheme::Em( 5.f ) );
    ImGui::DragFloat( "Speed", &sequence.Speed, 0.05f, 0.f, 16.f, "%.2fx", ImGuiSliderFlags_AlwaysClamp );
    if( IsAsset() )
    {
        ImGui::SeparatorText( "Object bindings" );
        for( const auto& slot : m_document->Data.GetSlots() )
        {
            ImGui::PushID( slot.c_str() );
            ImGui::TextUnformatted( slot.c_str() );
            EntityHandle target;
            for( const auto& binding : sequence.Bindings ) if( binding.Slot == slot ) target = binding.Target;
            const auto old = target;
            ImGui::SetNextItemWidth( -1.f );
            TargetPicker( "##Binding", target, world );
            if( target != old ) sequence.SetBinding( slot, target );
            ImGui::PopID();
        }
    }
}


void SequenceWidget::DrawClipLibrary( Sequence& sequence )
{
    ImGui::TextWrapped( "Give animations names to play them from C# or native code. All clips use this player's object bindings." );
    if( !m_owner ) { ImGui::TextDisabled( "Choose a scene player to add clips." ); return; }
    ImGui::BeginDisabled( sequence.Clip.empty() );
    if( ImGui::Button( "+ Add Current Clip", ImVec2( -1.f, 0.f ) ) )
    {
        std::string base = sequence.Name.empty() ? "Animation" : sequence.Name;
        std::string name = base;
        int suffix = 2;
        while( std::any_of( sequence.Clips.begin(), sequence.Clips.end(), [&]( const SequenceClipEntry& entry ) { return entry.Name == name; } ) ) name = base + " " + std::to_string( suffix++ );
        sequence.Clips.push_back( { name, sequence.Clip } );
    }
    ImGui::EndDisabled();
    for( size_t i = 0; i < sequence.Clips.size(); ++i )
    {
        auto& entry = sequence.Clips[i];
        ImGui::PushID( static_cast<int>( i ) );
        ImGui::Separator();
        ImGui::SetNextItemWidth( std::max( ImGuiTheme::Em( 3.f ), ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeightWithSpacing() ) );
        ImGui::InputText( "##Name", &entry.Name ); ImGui::SameLine();
        const bool erase = ImGui::Button( "x", ImGuiTheme::IconButtonSize() );
        ImGui::SetItemTooltip( "Remove named clip" );
        ImGui::TextDisabled( "%s", entry.Clip.empty() ? "No asset assigned" : std::filesystem::path( entry.Clip ).filename().string().c_str() );
        ImGui::SetItemTooltip( "%s", entry.Clip.c_str() );
        if( ImGui::SmallButton( "Choose asset..." ) )
        {
            const EntityHandle owner = m_owner;
            RequestAssetSelectionEvent event( [owner, i]( Path path ) {
                if( !owner || !owner->HasComponent<Sequence>() ) return;
                auto& player = owner->GetComponent<Sequence>();
                if( i >= player.Clips.size() ) return;
                const json before = EditorOps::CaptureComponent( *owner.Get(), "Sequence" );
                player.Clips[i].Clip = path.GetLocalPathString();
                EditorOps::RecordComponentEdit( *owner.Get(), "Sequence", before, EditorOps::CaptureComponent( *owner.Get(), "Sequence" ), "Assign Named Clip" );
            }, AssetType::Sequence ); event.Fire();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled( entry.Clip.empty() );
        if( ImGui::SmallButton( "Use as default" ) ) m_assignRequested = entry.Clip;
        ImGui::EndDisabled();
        if( !sequence.HasClip( entry.Name ) ) ImGui::TextColored( ImGuiTheme::WarningText(), "Use a unique name and choose an asset." );
        ImGui::PopID();
        if( erase ) { sequence.Clips.erase( sequence.Clips.begin() + i ); break; }
    }
    ImGuiTheme::SectionGap();
    ImGui::TextDisabled( "C#" );
    ImGui::TextUnformatted( "sequence.Play(\"DollyZoom\");" );
    ImGui::TextDisabled( "C++" );
    ImGui::TextUnformatted( "sequence.PlayClip(\"DollyZoom\");" );
}


bool SequenceWidget::ExecuteAutomation( const std::string& command, const std::string& args, std::string& error )
{
    error.clear();
    if( command == "seq-open" ) return OpenAsset( args );
    const auto selected = Selection::Get().GetActive();
    if( m_selectionVersion != Selection::Get().GetVersion() )
    {
        m_selectionVersion = Selection::Get().GetVersion();
        if( selected && selected->HasComponent<Sequence>() ) SetOwner( selected );
    }
    Sequence* sequence = Working();
    if( !sequence ) { error = "No sequence selected"; return false; }
    FinishEdit();
    std::string values = args; std::replace( values.begin(), values.end(), '|', ' ' ); std::istringstream input( values );
    if( command == "seq-export" ) return ExportAsset( args );
    if( command == "seq-save" ) return m_document && m_document->Save();
    if( command == "seq-undo" || command == "seq-redo" ) { if( !m_document ) return false; ExecuteAction( command == "seq-undo" ? "Edit.Undo" : "Edit.Redo" ); return true; }
    if( command == "seq-assert-dirty" ) { int expected = 0; input >> expected; if( HasUnsaved() != bool( expected ) ) { error = "Unexpected asset dirty state"; return false; } return true; }
    if( command == "seq-box-select" ) { float a, b; int c, d; if( !( input >> a >> b >> c >> d ) ) return false; m_timeline.SelectBox( *sequence, a, b, c, d ); return true; }
    if( command == "seq-assert-selected" ) { size_t count; if( !( input >> count ) || m_timeline.Selected.size() != count ) { error = "Unexpected selected key count"; return false; } return true; }
    if( command == "seq-move-keys" ) { int delta; if( !( input >> delta ) ) return false; const json before = EditState(); if( !m_timeline.Move( *sequence, delta ) ) { error = m_timeline.Message; return false; } CommitEdit( before ); return true; }
    if( command == "seq-delete-keys" ) { const json before = EditState(); m_timeline.Delete( *sequence ); CommitEdit( before ); return true; }
    if( command == "seq-length" ) { int length; if( !( input >> length ) ) return false; const json before = EditState(); sequence->DurationFrames = length; CommitEdit( before ); return true; }
    // Event markers: "|"-separated fields, so names and payloads may contain spaces.
    if( command.rfind( "seq-", 0 ) == 0 && ( command.find( "marker" ) != std::string::npos || command.find( "cut" ) != std::string::npos ) )
    {
        std::vector<std::string> fields;
        for( size_t start = 0;; )
        {
            const size_t bar = args.find( '|', start );
            std::string field = args.substr( start, bar == std::string::npos ? std::string::npos : bar - start );
            field.erase( 0, field.find_first_not_of( " \t" ) );
            field.erase( field.find_last_not_of( " \t" ) + 1 );
            fields.push_back( field );
            if( bar == std::string::npos ) break;
            start = bar + 1;
        }
        auto number = [&]( size_t i, int fallback ) { return i < fields.size() && !fields[i].empty() ? std::atoi( fields[i].c_str() ) : fallback; };
        auto marker = [&]( size_t i ) -> SequenceMarker* {
            const int index = number( i, -1 );
            if( index < 0 || index >= static_cast<int>( sequence->Markers.size() ) ) { error = "No event marker " + std::to_string( index ); return nullptr; }
            return &sequence->Markers[index];
        };
        auto entityNamed = [&]( const std::string& InName ) {
            EntityHandle found;
            EditorOps::GetWorld().ForEachEntity( [&]( Entity& entity ) { if( !found && entity.GetName() == InName ) found = entity.GetHandle(); } );
            if( !found ) error = "No entity named " + InName;
            return found;
        };
        auto cutAt = [&]( size_t i ) -> SequenceCut* {
            const int index = number( i, -1 );
            if( index < 0 || index >= static_cast<int>( sequence->Cuts.size() ) ) { error = "No camera cut " + std::to_string( index ); return nullptr; }
            return &sequence->Cuts[index];
        };
        const json before = EditState();
        // seq-add-cut frame | camera name
        if( command == "seq-add-cut" )
        {
            m_timeline.CutCamera = fields.size() > 1 && !fields[1].empty() ? entityNamed( fields[1] ) : EntityHandle();
            if( !error.empty() ) return false;
            if( m_timeline.AddCut( *sequence, number( 0, 0 ) ) < 0 ) { error = m_timeline.Message; return false; }
            if( IsAsset() ) sequence->PrepareAssetBindings();
            CommitEdit( before );
            return true;
        }
        // seq-move-cut index | frame
        if( command == "seq-move-cut" )
        {
            if( !cutAt( 0 ) ) return false;
            if( !m_timeline.MoveCut( *sequence, number( 0, 0 ), number( 1, 0 ) ) ) { error = m_timeline.Message; return false; }
            CommitEdit( before );
            return true;
        }
        if( command == "seq-delete-cut" )
        {
            if( !cutAt( 0 ) ) return false;
            m_timeline.SelectedCut = number( 0, 0 );
            m_timeline.DeleteCut( *sequence );
            CommitEdit( before );
            return true;
        }
        // seq-assert-cut index | frame | camera name
        if( command == "seq-assert-cut" )
        {
            const SequenceCut* found = cutAt( 0 );
            if( !found ) return false;
            const std::string camera = found->Target ? found->Target->GetName() : std::string( "<none>" );
            if( found->Frame != number( 1, -1 ) || ( fields.size() > 2 && camera != fields[2] ) )
            {
                error = "Camera cut is " + std::to_string( found->Frame ) + " | " + camera;
                return false;
            }
            return true;
        }
        // seq-assert-cut-camera name: the camera the Game View renders now.
        if( command == "seq-assert-cut-camera" )
        {
            const std::string current = Camera::CurrentCamera && Camera::CurrentCamera->Parent ? Camera::CurrentCamera->Parent->GetName() : std::string( "<none>" );
            if( current != args ) { error = "The current camera is " + current; return false; }
            return true;
        }
        if( command == "seq-assert-cut-count" )
        {
            if( static_cast<int>( sequence->Cuts.size() ) != number( 0, -1 ) ) { error = "There are " + std::to_string( sequence->Cuts.size() ) + " camera cuts"; return false; }
            return true;
        }
        // seq-add-marker frame | name [| payload [| number]]
        if( command == "seq-add-marker" )
        {
            if( fields.size() < 2 || fields[1].empty() ) { error = "seq-add-marker frame | name [| payload [| number]]"; return false; }
            SequenceMarker& added = sequence->Markers[m_timeline.AddMarker( *sequence, number( 0, 0 ) )];
            added.Name = fields[1];
            if( fields.size() > 2 ) added.Payload = fields[2];
            if( fields.size() > 3 ) added.Number = static_cast<float>( std::atof( fields[3].c_str() ) );
            CommitEdit( before );
            return true;
        }
        // seq-move-marker index | frame
        if( command == "seq-move-marker" )
        {
            if( !marker( 0 ) ) return false;
            m_timeline.MoveMarker( *sequence, number( 0, 0 ), number( 1, 0 ) );
            CommitEdit( before );
            return true;
        }
        if( command == "seq-delete-marker" )
        {
            if( !marker( 0 ) ) return false;
            m_timeline.SelectedMarker = number( 0, 0 );
            m_timeline.DeleteMarker( *sequence );
            CommitEdit( before );
            return true;
        }
        // seq-marker-receiver index | entity name (empty: the player)
        if( command == "seq-marker-receiver" )
        {
            SequenceMarker* found = marker( 0 );
            if( !found ) return false;
            EntityHandle target;
            const std::string name = fields.size() > 1 ? fields[1] : std::string();
            if( !name.empty() )
            {
                EditorOps::GetWorld().ForEachEntity( [&]( Entity& entity ) { if( !target && entity.GetName() == name ) target = entity.GetHandle(); } );
                if( !target ) { error = "No entity named " + name; return false; }
            }
            SetMarkerReceiver( *sequence, *found, target );
            CommitEdit( before );
            return true;
        }
        // seq-marker-action index | Event | PlayAudio | StopAudio | PlayAnimation [| payload [| number]]
        if( command == "seq-marker-action" )
        {
            SequenceMarker* found = marker( 0 );
            if( !found ) return false;
            const std::string name = fields.size() > 1 ? fields[1] : std::string();
            const std::pair<const char*, SequenceMarkerAction> actions[] = { { "Event", SequenceMarkerAction::Event }, { "PlayAudio", SequenceMarkerAction::PlayAudio },
                { "StopAudio", SequenceMarkerAction::StopAudio }, { "PlayAnimation", SequenceMarkerAction::PlayAnimation } };
            const auto match = std::find_if( std::begin( actions ), std::end( actions ), [&]( const auto& a ) { return name == a.first; } );
            if( match == std::end( actions ) ) { error = "Unknown marker action " + name; return false; }
            found->Action = match->second;
            if( fields.size() > 2 ) found->Payload = fields[2];
            if( fields.size() > 3 ) found->Number = static_cast<float>( std::atof( fields[3].c_str() ) );
            CommitEdit( before );
            return true;
        }
        // seq-assert-marker index | frame | name [| receiver name]
        if( command == "seq-assert-marker" )
        {
            const SequenceMarker* found = marker( 0 );
            if( !found ) return false;
            const EntityHandle receiver = found->ResolveReceiver( m_owner );
            const std::string receiverName = receiver ? receiver->GetName() : std::string( "<unbound>" );
            if( found->Frame != number( 1, -1 ) || ( fields.size() > 2 && found->Name != fields[2] ) || ( fields.size() > 3 && receiverName != fields[3] ) )
            {
                error = "Event marker is " + std::to_string( found->Frame ) + " | " + found->Name + " | " + receiverName;
                return false;
            }
            return true;
        }
        if( command == "seq-assert-marker-count" )
        {
            if( static_cast<int>( sequence->Markers.size() ) != number( 0, -1 ) ) { error = "There are " + std::to_string( sequence->Markers.size() ) + " event markers"; return false; }
            return true;
        }
    }
    if( command == "seq-copy-keys" ) { if( !m_timeline.Copy( *sequence ) ) { error = "No keys selected"; return false; } return true; }
    if( command == "seq-paste-keys" || command == "seq-duplicate-keys" )
    {
        int frame = m_frame;
        if( command == "seq-paste-keys" && !( input >> frame ) ) { error = "seq-paste-keys frame"; return false; }
        const json before = EditState();
        if( !( command == "seq-paste-keys" ? m_timeline.Paste( *sequence, frame ) : m_timeline.Duplicate( *sequence ) ) ) { error = m_timeline.Message; return false; }
        CommitEdit( before );
        return true;
    }
    // seq-lock-track row | 0/1, seq-mute-track row | 0/1
    if( command == "seq-lock-track" || command == "seq-mute-track" )
    {
        size_t row; int on;
        if( !( input >> row >> on ) || row >= sequence->Tracks.size() ) { error = command + " row | 0/1"; return false; }
        const json before = EditState();
        if( command == "seq-lock-track" ) sequence->Tracks[row].Locked = on != 0; else { sequence->StopPreview(); sequence->Tracks[row].Enabled = on == 0; }
        CommitEdit( before );
        return true;
    }
    if( command == "seq-assert-key" || command == "seq-assert-no-key" )
    {
        size_t row; int frame;
        if( !( input >> row >> frame ) || row >= sequence->Tracks.size() ) return false;
        const bool exists = std::any_of( sequence->Tracks[row].Keys.begin(), sequence->Tracks[row].Keys.end(), [&]( const SequenceKey& key ) { return key.Frame == frame; } );
        if( exists != ( command == "seq-assert-key" ) ) { error = "Unexpected key frame"; return false; } return true;
    }
    error = "Unknown sequence command";
    return false;
}
#endif
