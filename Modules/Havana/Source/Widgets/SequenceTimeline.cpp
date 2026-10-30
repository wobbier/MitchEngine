#include "SequenceTimeline.h"
#if USING( ME_EDITOR )
#include <imgui_internal.h>
#include <ImGuiTheme.h>
#include <algorithm>
#include <cmath>

void SequenceTimeline::Reset()
{
    Selected.clear();
    SelectedMarker = -1;
    SelectedCut = -1;
    m_original.clear();
    m_originalSelection.clear();
    m_box = m_drag = m_scrub = m_markerDrag = m_cutDrag = false;
    m_fit = true;
    Message.clear();
}


ImVec2 SequenceTimeline::KeyPosition( float InFrame, float InRow ) const
{
    return ImVec2( m_gridOrigin.x + ( InFrame - m_firstFrame ) * m_pixelsPerFrame, m_gridOrigin.y + ( InRow + 0.5f ) * m_rowHeight );
}


ImVec2 SequenceTimeline::MarkerPosition( float InFrame ) const
{
    return ImVec2( m_gridOrigin.x + ( InFrame - m_firstFrame ) * m_pixelsPerFrame, m_markerLaneY );
}


int SequenceTimeline::AddMarker( Sequence& InSequence, int InFrame )
{
    InSequence.StopPreview();
    SequenceMarker marker;
    marker.Frame = std::clamp( InFrame, 0, InSequence.DurationFrames );
    int suffix = static_cast<int>( InSequence.Markers.size() ) + 1;
    auto taken = [&]( const std::string& InName ) {
        return std::any_of( InSequence.Markers.begin(), InSequence.Markers.end(), [&]( const SequenceMarker& m ) { return m.Name == InName; } );
    };
    do marker.Name = "Event " + std::to_string( suffix++ ); while( taken( marker.Name ) );
    // After every marker already on that frame, so it fires last among them.
    auto at = std::upper_bound( InSequence.Markers.begin(), InSequence.Markers.end(), marker.Frame, []( int f, const SequenceMarker& m ) { return f < m.Frame; } );
    const auto inserted = InSequence.Markers.insert( at, marker );
    SelectedMarker = static_cast<int>( inserted - InSequence.Markers.begin() );
    Selected.clear();
    return SelectedMarker;
}


bool SequenceTimeline::MoveMarker( Sequence& InSequence, int InIndex, int InFrame )
{
    if( InIndex < 0 || InIndex >= static_cast<int>( InSequence.Markers.size() ) ) return false;
    InSequence.StopPreview();
    SequenceMarker marker = InSequence.Markers[InIndex];
    marker.Frame = std::clamp( InFrame, 0, InSequence.DurationFrames );
    InSequence.Markers.erase( InSequence.Markers.begin() + InIndex );
    // Keeps the list sorted; a marker dropped onto others fires after them.
    auto at = std::upper_bound( InSequence.Markers.begin(), InSequence.Markers.end(), marker.Frame, []( int f, const SequenceMarker& m ) { return f < m.Frame; } );
    const auto inserted = InSequence.Markers.insert( at, marker );
    SelectedMarker = static_cast<int>( inserted - InSequence.Markers.begin() );
    return true;
}


bool SequenceTimeline::DeleteMarker( Sequence& InSequence )
{
    if( SelectedMarker < 0 || SelectedMarker >= static_cast<int>( InSequence.Markers.size() ) ) return false;
    InSequence.StopPreview();
    InSequence.Markers.erase( InSequence.Markers.begin() + SelectedMarker );
    SelectedMarker = -1;
    return true;
}


bool SequenceTimeline::Copy( const Sequence& InSequence )
{
    if( Selected.empty() ) return false;
    m_clipboard = SequenceEditing::Copy( InSequence.Tracks, Selected );
    Message = "Copied " + std::to_string( m_clipboard.size() ) + " keys";
    return true;
}


bool SequenceTimeline::Paste( Sequence& InSequence, int InFrame )
{
    SequenceEditing::Selection pasted;
    auto tracks = InSequence.Tracks;
    if( !SequenceEditing::Paste( tracks, m_clipboard, InFrame, InSequence.DurationFrames, pasted ) )
    {
        Message = m_clipboard.empty() ? "Nothing to paste: copy keys first." : "Paste blocked: past the end, or onto a missing or locked track.";
        return false;
    }
    InSequence.StopPreview();
    InSequence.Tracks = std::move( tracks );
    Selected = std::move( pasted );
    Message.clear();
    return true;
}


bool SequenceTimeline::Duplicate( Sequence& InSequence )
{
    SequenceEditing::Selection copies;
    auto tracks = InSequence.Tracks;
    if( !SequenceEditing::Duplicate( tracks, Selected, InSequence.DurationFrames, copies ) )
    {
        Message = Selected.empty() ? "Select keys to duplicate." : "Duplicate blocked: no room after the selection.";
        return false;
    }
    InSequence.StopPreview();
    InSequence.Tracks = std::move( tracks );
    Selected = std::move( copies );
    Message.clear();
    return true;
}


ImVec2 SequenceTimeline::TogglePosition( int InRow, bool InLock ) const
{
    const float size = m_rowHeight * 0.5f;
    return ImVec2( m_legendRight - size * ( InLock ? 0.6f : 1.8f ), m_gridOrigin.y + ( InRow + 0.5f ) * m_rowHeight );
}


ImVec2 SequenceTimeline::CutPosition( float InFrame ) const
{
    return ImVec2( m_gridOrigin.x + ( InFrame - m_firstFrame ) * m_pixelsPerFrame, m_cutLaneY );
}


int SequenceTimeline::AddCut( Sequence& InSequence, int InFrame )
{
    const int frame = std::clamp( InFrame, 0, InSequence.DurationFrames );
    for( const SequenceCut& cut : InSequence.Cuts ) if( cut.Frame == frame ) { Message = "There is already a camera cut on that frame."; return -1; }
    InSequence.StopPreview();
    SequenceCut cut;
    cut.Frame = frame;
    cut.Target = CutCamera;
    auto at = std::upper_bound( InSequence.Cuts.begin(), InSequence.Cuts.end(), frame, []( int f, const SequenceCut& c ) { return f < c.Frame; } );
    const auto inserted = InSequence.Cuts.insert( at, cut );
    SelectedCut = static_cast<int>( inserted - InSequence.Cuts.begin() );
    SelectedMarker = -1;
    Selected.clear();
    return SelectedCut;
}


bool SequenceTimeline::MoveCut( Sequence& InSequence, int InIndex, int InFrame )
{
    if( InIndex < 0 || InIndex >= static_cast<int>( InSequence.Cuts.size() ) ) return false;
    const int frame = std::clamp( InFrame, 0, InSequence.DurationFrames );
    for( int i = 0; i < static_cast<int>( InSequence.Cuts.size() ); ++i )
        if( i != InIndex && InSequence.Cuts[i].Frame == frame ) { Message = "Move blocked: that frame already has a camera cut."; return false; }
    InSequence.StopPreview();
    SequenceCut cut = InSequence.Cuts[InIndex];
    cut.Frame = frame;
    InSequence.Cuts.erase( InSequence.Cuts.begin() + InIndex );
    auto at = std::upper_bound( InSequence.Cuts.begin(), InSequence.Cuts.end(), frame, []( int f, const SequenceCut& c ) { return f < c.Frame; } );
    const auto inserted = InSequence.Cuts.insert( at, cut );
    SelectedCut = static_cast<int>( inserted - InSequence.Cuts.begin() );
    Message.clear();
    return true;
}


bool SequenceTimeline::DeleteCut( Sequence& InSequence )
{
    if( SelectedCut < 0 || SelectedCut >= static_cast<int>( InSequence.Cuts.size() ) ) return false;
    InSequence.StopPreview();
    InSequence.Cuts.erase( InSequence.Cuts.begin() + SelectedCut );
    SelectedCut = -1;
    return true;
}


void SequenceTimeline::SelectBox( const Sequence& InSequence, float InFirst, float InLast, int InFirstRow, int InLastRow, bool InAdd )
{
    SequenceEditing::BoxSelect( InSequence.Tracks, InFirst, InLast, InFirstRow, InLastRow, Selected, InAdd );
}


bool SequenceTimeline::Move( Sequence& InSequence, int InDelta )
{
    std::vector<SequenceTrack> moved;
    SequenceEditing::Selection selected;
    if( !SequenceEditing::Move( InSequence.Tracks, Selected, InDelta, InSequence.DurationFrames, moved, selected ) )
    {
        Message = "Move blocked: a destination frame already has a key.";
        return false;
    }
    InSequence.StopPreview();
    InSequence.Tracks = std::move( moved );
    Selected = std::move( selected );
    Message.clear();
    return true;
}


bool SequenceTimeline::Delete( Sequence& InSequence )
{
    InSequence.StopPreview();
    const bool changed = SequenceEditing::Delete( InSequence.Tracks, Selected );
    Selected.clear();
    return changed;
}


bool SequenceTimeline::Draw( Sequence& sequence, int& frame, int& track )
{
    bool changed = false;
    if( ImGui::Button( "Fit" ) ) m_fit = true;
    ImGui::SetItemTooltip( "Fit the entire clip in the timeline" );
    ImGui::SameLine();
    if( SelectedMarker >= static_cast<int>( sequence.Markers.size() ) ) SelectedMarker = -1;
    if( SelectedCut >= static_cast<int>( sequence.Cuts.size() ) ) SelectedCut = -1;
    ImGui::BeginDisabled( Selected.empty() );
    if( ImGui::Button( "<" ) ) changed |= Move( sequence, -1 );
    ImGui::SetItemTooltip( "Move selected keys one frame left" );
    ImGui::SameLine();
    if( ImGui::Button( ">" ) ) changed |= Move( sequence, 1 );
    ImGui::SetItemTooltip( "Move selected keys one frame right" );
    ImGui::SameLine();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled( Selected.empty() && SelectedMarker < 0 && SelectedCut < 0 );
    if( ImGui::Button( "Delete" ) ) changed |= !Selected.empty() ? Delete( sequence ) : SelectedMarker >= 0 ? DeleteMarker( sequence ) : DeleteCut( sequence );
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SetNextItemWidth( ImGuiTheme::Em( 3.8f ) );
    ImGui::DragInt( "##Snap", &m_snap, 1.f, 1, 120, "%d f", ImGuiSliderFlags_AlwaysClamp );
    ImGui::SetItemTooltip( "Key drag snapping in frames" );
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled( "Snap" );
    ImGui::SameLine();
    ImGui::TextDisabled( "(?)" );
    ImGui::SetItemTooltip( "Box select: drag empty space\nAdd selection: Shift\nToggle key: Ctrl/Cmd-click\nMove selection: drag a selected key (snaps to the playhead, markers and cuts)\nCopy / paste at playhead / duplicate: Ctrl+C / Ctrl+V / Ctrl+D\nAdd event marker: double-click the Events lane\nAdd camera cut: double-click the Cameras lane\nZoom: Ctrl/Cmd + wheel\nPan: middle drag\nCancel drag: Escape" );
    if( !Message.empty() ) ImGui::TextColored( ImGuiTheme::WarningText(), "%s", Message.c_str() );

    ImGui::PushStyleColor( ImGuiCol_ChildBg, ImGuiTheme::ActivePalette().Recessed );
    ImGui::BeginChild( "Sequence Canvas", ImVec2( 0.f, 0.f ), ImGuiChildFlags_Border );
    ImGui::PopStyleColor();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = std::max( ImGui::GetContentRegionAvail().x, ImGuiTheme::Em( 6.f ) );
    const float rowHeight = ImGuiTheme::Em( 2.9f );
    const float header = ImGuiTheme::Em( 2.1f );
    const float markerLane = ImGuiTheme::Em( 1.9f );
    const float cutsTop = origin.y + header + markerLane;
    const float tracksTop = cutsTop + markerLane;
    const float legend = std::clamp( width * 0.3f, ImGuiTheme::Em( 8.f ), ImGuiTheme::Em( 15.f ) );
    const float grid = origin.x + legend;
    const float gridWidth = std::max( width - legend - 10.f, 20.f );
    m_gridOrigin = ImVec2( grid, tracksTop );
    m_legendRight = grid - ImGuiTheme::Em( 0.4f );
    m_markerLaneY = origin.y + header + markerLane * 0.5f;
    m_cutLaneY = cutsTop + markerLane * 0.5f;
    m_rowHeight = rowHeight;
    const float height = std::max( header + markerLane * 2.f + rowHeight * sequence.Tracks.size(), ImGui::GetContentRegionAvail().y );
    if( m_fit || m_pixelsPerFrame <= 0.f )
    {
        m_pixelsPerFrame = std::clamp( gridWidth / std::max( sequence.DurationFrames, 1 ), 0.05f, 80.f );
        m_firstFrame = 0.f;
        m_fit = false;
    }
    ImGui::InvisibleButton( "##keys", ImVec2( width, height ), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle );
    // Entering runtime can disable the canvas in the middle of a pointer gesture.
    if( ImGui::GetItemFlags() & ImGuiItemFlags_Disabled ) m_drag = m_box = m_scrub = m_markerDrag = m_cutDrag = false;
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImGuiIO& io = ImGui::GetIO();
    const bool control = io.KeyCtrl || io.KeySuper;
    auto toFrame = [&]( float x ) { return m_firstFrame + ( x - grid ) / m_pixelsPerFrame; };
    auto toRow = [&]( float y ) { return static_cast<int>( std::floor( ( y - tracksTop ) / rowHeight ) ); };
    auto keyX = [&]( float f ) { return grid + ( f - m_firstFrame ) * m_pixelsPerFrame; };
    auto keyY = [&]( int row ) { return tracksTop + ( row + 0.5f ) * rowHeight; };

    if( hovered && control && io.MouseWheel != 0.f )
    {
        const float anchor = toFrame( io.MousePos.x );
        m_pixelsPerFrame = std::clamp( m_pixelsPerFrame * std::pow( 1.2f, io.MouseWheel ), 0.05f, 80.f );
        m_firstFrame = std::max( 0.f, anchor - ( io.MousePos.x - grid ) / m_pixelsPerFrame );
    }
    if( active && ImGui::IsMouseDragging( ImGuiMouseButton_Middle, 0.f ) ) m_firstFrame = std::max( 0.f, m_firstFrame - io.MouseDelta.x / m_pixelsPerFrame );
    if( hovered && io.MouseWheelH != 0.f ) m_firstFrame = std::max( 0.f, m_firstFrame - io.MouseWheelH * 10.f );

    SequenceEditing::Key hit;
    bool hasHit = false;
    const int mouseRow = toRow( io.MousePos.y );
    const float hitRadius = ImGuiTheme::Em( 0.5f );
    if( mouseRow >= 0 && mouseRow < static_cast<int>( sequence.Tracks.size() ) && io.MousePos.x >= grid && !sequence.Tracks[mouseRow].Locked )
        for( const auto& key : sequence.Tracks[mouseRow].Keys )
            if( std::abs( io.MousePos.x - keyX( key.Frame ) ) <= hitRadius && std::abs( io.MousePos.y - keyY( mouseRow ) ) <= hitRadius ) { hit = { static_cast<size_t>( mouseRow ), key.Frame }; hasHit = true; break; }

    const bool inMarkerLane = io.MousePos.y >= origin.y + header && io.MousePos.y < cutsTop;
    const bool inCutLane = io.MousePos.y >= cutsTop && io.MousePos.y < tracksTop;
    int cutHit = -1;
    if( inCutLane && io.MousePos.x >= grid )
        for( int i = 0; i < static_cast<int>( sequence.Cuts.size() ) && cutHit < 0; ++i )
            if( std::abs( io.MousePos.x - keyX( static_cast<float>( sequence.Cuts[i].Frame ) ) ) <= hitRadius ) cutHit = i;
    int markerHit = -1;
    if( inMarkerLane && io.MousePos.x >= grid )
        for( int i = static_cast<int>( sequence.Markers.size() ) - 1; i >= 0 && markerHit < 0; --i )
            if( std::abs( io.MousePos.x - keyX( static_cast<float>( sequence.Markers[i].Frame ) ) ) <= hitRadius ) markerHit = i;

    if( hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
    {
        Message.clear();
        m_start = io.MousePos;
        if( io.MousePos.x < grid )
        {
            if( mouseRow >= 0 && mouseRow < static_cast<int>( sequence.Tracks.size() ) )
            {
                track = mouseRow;
                const float half = m_rowHeight * 0.3f;
                auto inside = [&]( ImVec2 c ) { return std::abs( io.MousePos.x - c.x ) <= half && std::abs( io.MousePos.y - c.y ) <= half; };
                SequenceTrack& lane = sequence.Tracks[mouseRow];
                if( inside( TogglePosition( mouseRow, false ) ) ) { sequence.StopPreview(); lane.Enabled = !lane.Enabled; changed = true; }
                else if( inside( TogglePosition( mouseRow, true ) ) )
                {
                    lane.Locked = !lane.Locked;
                    if( lane.Locked ) Selected.erase( std::remove_if( Selected.begin(), Selected.end(), [&]( const SequenceEditing::Key& k ) { return k.Track == static_cast<size_t>( mouseRow ); } ), Selected.end() );
                    changed = true;
                }
            }
        }
        else if( io.MousePos.y < origin.y + header ) m_scrub = true;
        else if( inCutLane )
        {
            Selected.clear();
            SelectedMarker = -1;
            if( cutHit >= 0 )
            {
                SelectedCut = cutHit;
                m_cutDrag = true;
                m_cutOriginal = sequence.Cuts[cutHit].Frame;
            }
            else if( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
                changed |= AddCut( sequence, static_cast<int>( std::lround( toFrame( io.MousePos.x ) / m_snap ) ) * m_snap ) >= 0;
            else SelectedCut = -1;
        }
        else if( inMarkerLane )
        {
            Selected.clear();
            SelectedCut = -1;
            if( markerHit >= 0 )
            {
                SelectedMarker = markerHit;
                m_markerDrag = true;
                m_markerOriginal = sequence.Markers[markerHit].Frame;
            }
            else if( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
            {
                AddMarker( sequence, static_cast<int>( std::lround( toFrame( io.MousePos.x ) / m_snap ) ) * m_snap );
                changed = true;
            }
            else SelectedMarker = -1;
        }
        else if( hasHit )
        {
            SelectedMarker = SelectedCut = -1;
            track = static_cast<int>( hit.Track );
            const bool selected = SequenceEditing::Contains( Selected, hit );
            if( control && selected ) Selected.erase( std::remove( Selected.begin(), Selected.end(), hit ), Selected.end() );
            else
            {
                if( !selected && !control && !io.KeyShift ) Selected.clear();
                if( !selected ) Selected.push_back( hit );
                m_drag = true;
                m_dragFrame = hit.Frame;
                m_original = sequence.Tracks;
                m_originalSelection = Selected;
            }
            if( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) ) { frame = hit.Frame; sequence.StartPreview( static_cast<float>( frame ) ); }
        }
        else
        {
            m_box = true;
            m_addBox = control || io.KeyShift;
            m_originalSelection = Selected;
            if( !m_addBox ) Selected.clear();
        }
    }
    if( m_scrub && io.MouseDown[0] )
    {
        frame = std::clamp( static_cast<int>( std::lround( toFrame( io.MousePos.x ) ) ), 0, sequence.DurationFrames );
        sequence.StartPreview( static_cast<float>( frame ) );
    }
    if( m_box && io.MouseDown[0] )
    {
        Selected = m_addBox ? m_originalSelection : SequenceEditing::Selection{};
        SelectBox( sequence, toFrame( std::min( m_start.x, io.MousePos.x ) ), toFrame( std::max( m_start.x, io.MousePos.x ) ),
            toRow( std::min( m_start.y, io.MousePos.y ) ), toRow( std::max( m_start.y, io.MousePos.y ) ), m_addBox );
    }
    if( m_markerDrag && SelectedMarker >= 0 && ImGui::IsMouseDragging( ImGuiMouseButton_Left ) )
    {
        const int delta = static_cast<int>( std::lround( ( io.MousePos.x - m_start.x ) / m_pixelsPerFrame / m_snap ) ) * m_snap;
        const int target = std::clamp( m_markerOriginal + delta, 0, sequence.DurationFrames );
        if( sequence.Markers[SelectedMarker].Frame != target ) changed |= MoveMarker( sequence, SelectedMarker, target );
    }
    if( m_cutDrag && SelectedCut >= 0 && ImGui::IsMouseDragging( ImGuiMouseButton_Left ) )
    {
        const int delta = static_cast<int>( std::lround( ( io.MousePos.x - m_start.x ) / m_pixelsPerFrame / m_snap ) ) * m_snap;
        const int target = std::clamp( m_cutOriginal + delta, 0, sequence.DurationFrames );
        if( sequence.Cuts[SelectedCut].Frame != target ) changed |= MoveCut( sequence, SelectedCut, target );
    }
    if( m_drag && ImGui::IsMouseDragging( ImGuiMouseButton_Left ) )
    {
        int delta = static_cast<int>( std::lround( ( io.MousePos.x - m_start.x ) / m_pixelsPerFrame / m_snap ) ) * m_snap;
        // The grabbed key snaps onto the playhead, markers and cuts when it's within reach.
        const float landing = m_dragFrame + ( io.MousePos.x - m_start.x ) / m_pixelsPerFrame;
        float bestDistance = ImGuiTheme::Em( 0.5f ) / m_pixelsPerFrame;
        auto snapTo = [&]( int target ) { if( std::abs( landing - target ) < bestDistance ) { bestDistance = std::abs( landing - target ); delta = target - m_dragFrame; } };
        snapTo( frame );
        for( const SequenceMarker& marker : sequence.Markers ) snapTo( marker.Frame );
        for( const SequenceCut& cut : sequence.Cuts ) snapTo( cut.Frame );
        std::vector<SequenceTrack> moved;
        SequenceEditing::Selection selected;
        if( SequenceEditing::Move( m_original, m_originalSelection, delta, sequence.DurationFrames, moved, selected ) )
        {
            sequence.StopPreview();
            sequence.Tracks = std::move( moved );
            Selected = std::move( selected );
            Message.clear();
            changed = true;
        }
        else Message = "Move blocked: a destination frame already has a key.";
    }
    if( active && ImGui::IsKeyPressed( ImGuiKey_Escape, false ) )
    {
        if( m_drag ) { sequence.Tracks = m_original; changed = true; }
        if( m_markerDrag && SelectedMarker >= 0 ) changed |= MoveMarker( sequence, SelectedMarker, m_markerOriginal );
        else if( m_cutDrag && SelectedCut >= 0 ) changed |= MoveCut( sequence, SelectedCut, m_cutOriginal );
        else Selected = m_originalSelection;
        m_drag = m_box = m_scrub = m_markerDrag = m_cutDrag = false;
    }
    if( !io.MouseDown[0] ) m_drag = m_box = m_scrub = m_markerDrag = m_cutDrag = false;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const auto& palette = ImGuiTheme::ActivePalette();
    const float inset = ImGuiTheme::Em( 0.6f );
    const float endX = keyX( static_cast<float>( sequence.DurationFrames ) );
    draw->AddRectFilled( origin, ImVec2( origin.x + width, origin.y + header ), ImGui::GetColorU32( palette.Header ) );
    draw->AddText( ImVec2( origin.x + inset, origin.y + ImGuiTheme::Em( 0.5f ) ), ImGui::GetColorU32( palette.Muted ), "TRACKS" );
    auto laneColour = [&]( const SequenceTrack& lane ) {
        return lane.Component == "Camera" ? palette.Info : lane.Property == "Rotation" ? ImVec4( 0.65f, 0.58f, 0.95f, 1.f ) :
            lane.Property == "Scale" ? palette.Success : palette.AccentText;
    };
    draw->AddRectFilled( ImVec2( origin.x, origin.y + header ), ImVec2( origin.x + width, tracksTop ), ImGui::GetColorU32( ImGuiTheme::Mix( palette.Recessed, palette.Text, 0.05f ) ) );
    draw->AddText( ImVec2( origin.x + inset, origin.y + header + ImGuiTheme::Em( 0.35f ) ), ImGui::GetColorU32( palette.Muted ), "EVENTS" );
    draw->AddLine( ImVec2( origin.x, cutsTop ), ImVec2( origin.x + width, cutsTop ), ImGui::GetColorU32( ImGuiTheme::WithAlpha( palette.Muted, 0.12f ) ) );
    draw->AddText( ImVec2( origin.x + inset, cutsTop + ImGuiTheme::Em( 0.35f ) ), ImGui::GetColorU32( palette.Muted ), "CAMERAS" );
    draw->AddLine( ImVec2( origin.x, tracksTop ), ImVec2( origin.x + width, tracksTop ), ImGui::GetColorU32( ImGuiTheme::WithAlpha( palette.Muted, 0.2f ) ) );
    for( size_t row = 0; row < sequence.Tracks.size(); ++row )
    {
        const float y = tracksTop + row * rowHeight;
        if( y + rowHeight < ImGui::GetWindowPos().y || y > ImGui::GetWindowPos().y + ImGui::GetWindowHeight() ) continue;
        const bool selected = static_cast<int>( row ) == track;
        const auto& lane = sequence.Tracks[row];
        const ImVec4 background = selected ? ImGuiTheme::Mix( palette.Recessed, palette.AccentText, 0.13f ) :
            ImGuiTheme::Mix( palette.Recessed, palette.Text, row % 2 ? 0.035f : 0.015f );
        draw->AddRectFilled( ImVec2( origin.x, y ), ImVec2( origin.x + width, y + rowHeight ), ImGui::GetColorU32( background ) );
        draw->AddLine( ImVec2( origin.x, y + rowHeight ), ImVec2( origin.x + width, y + rowHeight ), ImGui::GetColorU32( ImGuiTheme::WithAlpha( palette.Muted, 0.12f ) ) );
        draw->AddRectFilled( ImVec2( origin.x, y + 1.f ), ImVec2( origin.x + 3.f, y + rowHeight - 1.f ), ImGui::GetColorU32( lane.Enabled ? laneColour( lane ) : palette.Muted ) );
        const std::string binding = lane.Binding.empty() ? ( lane.Target ? lane.Target->GetName() : "Unbound" ) : lane.Binding;
        const std::string property = lane.Component + "." + lane.Property;
        draw->PushClipRect( ImVec2( origin.x, y ), ImVec2( TogglePosition( static_cast<int>( row ), false ).x - rowHeight * 0.32f, y + rowHeight ), true );
        draw->AddText( ImVec2( origin.x + inset, y + ImGuiTheme::Em( 0.35f ) ), ImGui::GetColorU32( lane.Enabled ? palette.Text : palette.Muted ), binding.c_str() );
        draw->AddText( ImVec2( origin.x + inset, y + ImGuiTheme::Em( 1.55f ) ), ImGui::GetColorU32( palette.Muted ), property.c_str() );
        draw->PopClipRect();
        // Mute (the track's Enabled) and Lock toggles at the legend's right edge.
        for( const bool lock : { false, true } )
        {
            const ImVec2 c = TogglePosition( static_cast<int>( row ), lock );
            const bool on = lock ? lane.Locked : !lane.Enabled;
            const float half = rowHeight * 0.26f;
            draw->AddRectFilled( ImVec2( c.x - half, c.y - half ), ImVec2( c.x + half, c.y + half ), ImGui::GetColorU32( on ? ImGuiTheme::WithAlpha( palette.Warning, 0.85f ) : ImGuiTheme::WithAlpha( palette.Muted, 0.18f ) ), 3.f );
            const char* glyph = lock ? "L" : "M";
            const ImVec2 size = ImGui::CalcTextSize( glyph );
            draw->AddText( ImVec2( c.x - size.x * 0.5f, c.y - size.y * 0.5f ), ImGui::GetColorU32( on ? palette.Recessed : palette.Muted ), glyph );
        }
    }
    draw->AddLine( ImVec2( grid - 1.f, origin.y ), ImVec2( grid - 1.f, origin.y + height ), ImGui::GetColorU32( palette.Separator ) );
    draw->PushClipRect( ImVec2( grid, origin.y ), ImVec2( origin.x + width, origin.y + height ), true );
    if( endX < origin.x + width ) draw->AddRectFilled( ImVec2( std::max( grid, endX ), origin.y + header ), ImVec2( origin.x + width, origin.y + height ), ImGui::GetColorU32( ImGuiTheme::WithAlpha( palette.Canvas, 0.35f ) ) );
    int step = 1;
    while( step * m_pixelsPerFrame < ImGuiTheme::Em( 4.5f ) && step < 100000 ) step = step == 1 ? 5 : step == 5 ? 10 : step * 2;
    const int first = std::max( 0, static_cast<int>( m_firstFrame ) / step * step );
    const int last = std::min( sequence.DurationFrames, static_cast<int>( toFrame( origin.x + width ) ) + step );
    const int minor = std::max( step / 5, 1 );
    for( int f = std::max( 0, first - step ); f <= last; f += minor )
    {
        const float x = keyX( static_cast<float>( f ) );
        const bool major = f % step == 0;
        draw->AddLine( ImVec2( x, origin.y + header - ( major ? 7.f : 4.f ) ), ImVec2( x, origin.y + height ),
            ImGui::GetColorU32( ImGuiTheme::WithAlpha( palette.Muted, major ? 0.18f : 0.065f ) ) );
        if( major )
        {
            const std::string label = std::to_string( f );
            draw->AddText( ImVec2( x + 4.f, origin.y + ImGuiTheme::Em( 0.4f ) ), ImGui::GetColorU32( palette.Muted ), label.c_str() );
        }
    }
    for( size_t row = 0; row < sequence.Tracks.size(); ++row )
        for( const auto& key : sequence.Tracks[row].Keys )
        {
            const ImVec2 p( keyX( static_cast<float>( key.Frame ) ), keyY( static_cast<int>( row ) ) );
            if( p.x < grid - 8.f || p.x > origin.x + width + 8.f || p.y < ImGui::GetWindowPos().y || p.y > ImGui::GetWindowPos().y + ImGui::GetWindowHeight() ) continue;
            const bool selected = SequenceEditing::Contains( Selected, { row, key.Frame } );
            const ImU32 colour = ImGui::GetColorU32( selected ? palette.Warning : sequence.Tracks[row].Enabled ? laneColour( sequence.Tracks[row] ) : palette.Muted );
            const float radius = ImGuiTheme::Em( selected ? 0.45f : 0.36f );
            draw->AddQuadFilled( ImVec2( p.x, p.y - radius ), ImVec2( p.x + radius, p.y ), ImVec2( p.x, p.y + radius ), ImVec2( p.x - radius, p.y ), colour );
            if( selected ) draw->AddQuad( ImVec2( p.x, p.y - radius ), ImVec2( p.x + radius, p.y ), ImVec2( p.x, p.y + radius ), ImVec2( p.x - radius, p.y ), ImGui::GetColorU32( palette.Text ) );
        }
    // Markers: a pennant in the lane and a faint rule down through the tracks.
    for( int i = 0; i < static_cast<int>( sequence.Markers.size() ); ++i )
    {
        const SequenceMarker& marker = sequence.Markers[i];
        const float x = keyX( static_cast<float>( marker.Frame ) );
        if( x < grid - 8.f || x > origin.x + width + 8.f ) continue;
        const bool selected = i == SelectedMarker;
        const ImVec4 tone = marker.Action == SequenceMarkerAction::Event ? palette.Info : marker.Action == SequenceMarkerAction::PlayAnimation ? ImVec4( 0.65f, 0.58f, 0.95f, 1.f ) : palette.Success;
        const ImU32 colour = ImGui::GetColorU32( selected ? palette.Warning : tone );
        const float top = origin.y + header + ImGuiTheme::Em( 0.3f );
        const float bottom = tracksTop - ImGuiTheme::Em( 0.3f );
        draw->AddLine( ImVec2( x, tracksTop ), ImVec2( x, origin.y + height ), ImGui::GetColorU32( ImGuiTheme::WithAlpha( selected ? palette.Warning : tone, 0.25f ) ) );
        draw->AddLine( ImVec2( x, top ), ImVec2( x, bottom ), colour, 1.5f );
        draw->AddTriangleFilled( ImVec2( x, top ), ImVec2( x + ImGuiTheme::Em( 0.7f ), top + ImGuiTheme::Em( 0.35f ) ), ImVec2( x, top + ImGuiTheme::Em( 0.7f ) ), colour );
        draw->AddText( ImVec2( x + ImGuiTheme::Em( 0.85f ), top - ImGuiTheme::Em( 0.1f ) ), ImGui::GetColorU32( selected ? palette.Text : palette.Muted ), marker.Name.c_str() );
    }
    // Cuts: a shot bar from each cut to the next (or the end), named after its camera.
    for( int i = 0; i < static_cast<int>( sequence.Cuts.size() ); ++i )
    {
        const SequenceCut& cut = sequence.Cuts[i];
        const float x = keyX( static_cast<float>( cut.Frame ) );
        const float next = keyX( static_cast<float>( i + 1 < static_cast<int>( sequence.Cuts.size() ) ? sequence.Cuts[i + 1].Frame : sequence.DurationFrames ) );
        if( next < grid || x > origin.x + width ) continue;
        const bool selected = i == SelectedCut;
        const ImVec4 tone = selected ? palette.Warning : cut.Target ? palette.Success : palette.Muted;
        const float top = cutsTop + ImGuiTheme::Em( 0.3f );
        const float bottom = tracksTop - ImGuiTheme::Em( 0.3f );
        draw->AddRectFilled( ImVec2( x, top ), ImVec2( std::max( next - 2.f, x + 3.f ), bottom ), ImGui::GetColorU32( ImGuiTheme::WithAlpha( tone, 0.28f ) ), 3.f );
        draw->AddRectFilled( ImVec2( x, top ), ImVec2( x + 3.f, bottom ), ImGui::GetColorU32( tone ) );
        const std::string label = cut.Target ? cut.Target->GetName() : cut.Binding.empty() ? std::string( "No camera" ) : cut.Binding + " (unbound)";
        draw->PushClipRect( ImVec2( x, top ), ImVec2( std::max( next - 4.f, x ), bottom ), true );
        draw->AddText( ImVec2( x + ImGuiTheme::Em( 0.45f ), top + ImGuiTheme::Em( 0.05f ) ), ImGui::GetColorU32( selected ? palette.Text : palette.Muted ), label.c_str() );
        draw->PopClipRect();
    }
    const float playhead = keyX( static_cast<float>( frame ) );
    const ImU32 playheadColour = ImGui::GetColorU32( palette.Error );
    draw->AddLine( ImVec2( playhead, origin.y + header - 7.f ), ImVec2( playhead, origin.y + height ), playheadColour, 1.5f );
    draw->AddTriangleFilled( ImVec2( playhead - 6.f, origin.y + header - 10.f ), ImVec2( playhead + 6.f, origin.y + header - 10.f ), ImVec2( playhead, origin.y + header - 3.f ), playheadColour );
    if( m_box )
    {
        const ImVec2 a( std::max( grid, std::min( m_start.x, io.MousePos.x ) ), std::min( m_start.y, io.MousePos.y ) );
        const ImVec2 b( std::max( m_start.x, io.MousePos.x ), std::max( m_start.y, io.MousePos.y ) );
        draw->AddRectFilled( a, b, ImGui::GetColorU32( ImVec4( 0.35f, 0.6f, 1.f, 0.15f ) ) );
        draw->AddRect( a, b, ImGui::GetColorU32( ImGuiCol_NavHighlight ) );
    }
    draw->PopClipRect();
    if( hovered && markerHit >= 0 && !m_markerDrag && !m_drag && !m_box )
    {
        const SequenceMarker& marker = sequence.Markers[markerHit];
        const std::string receiver = marker.Target ? marker.Target->GetName() : marker.Binding.empty() ? "this player" : marker.Binding + " (unbound)";
        ImGui::SetTooltip( "%s\nFrame %d  |  %.2f s\nReceiver: %s%s%s", marker.Name.c_str(), marker.Frame, float( marker.Frame ) / sequence.FrameRate, receiver.c_str(),
            marker.Payload.empty() ? "" : "\nPayload: ", marker.Payload.c_str() );
        ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeEW );
    }
    else if( hovered && io.MousePos.x < grid && mouseRow >= 0 && mouseRow < static_cast<int>( sequence.Tracks.size() ) )
    {
        const float half = m_rowHeight * 0.3f;
        for( const bool lock : { false, true } )
        {
            const ImVec2 c = TogglePosition( mouseRow, lock );
            if( std::abs( io.MousePos.x - c.x ) <= half && std::abs( io.MousePos.y - c.y ) <= half )
                ImGui::SetTooltip( "%s", lock ? "Lock: keys can't be selected or edited" : "Mute: the track doesn't animate" );
        }
    }
    else if( hovered && hasHit && !m_drag && !m_box )
    {
        ImGui::SetTooltip( "%s.%s\nFrame %d  |  %.2f s", sequence.Tracks[hit.Track].Component.c_str(), sequence.Tracks[hit.Track].Property.c_str(), hit.Frame, float( hit.Frame ) / sequence.FrameRate );
        ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeEW );
    }
    if( sequence.Tracks.empty() )
        draw->AddText( ImVec2( grid + inset, tracksTop + inset ), ImGui::GetColorU32( palette.Muted ), "Add a track to begin animating" );
    ImGui::EndChild();
    return changed;
}
#endif
