#include "SequenceTimeline.h"
#if USING( ME_EDITOR )
#include <imgui_internal.h>
#include <ImGuiTheme.h>
#include <algorithm>
#include <cmath>

void SequenceTimeline::Reset()
{
    Selected.clear();
    m_original.clear();
    m_originalSelection.clear();
    m_box = m_drag = m_scrub = false;
    m_fit = true;
    Message.clear();
}


ImVec2 SequenceTimeline::KeyPosition( float InFrame, float InRow ) const
{
    return ImVec2( m_gridOrigin.x + ( InFrame - m_firstFrame ) * m_pixelsPerFrame, m_gridOrigin.y + ( InRow + 0.5f ) * m_rowHeight );
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
    ImGui::BeginDisabled( Selected.empty() );
    if( ImGui::Button( "<" ) ) changed |= Move( sequence, -1 );
    ImGui::SetItemTooltip( "Move selected keys one frame left" );
    ImGui::SameLine();
    if( ImGui::Button( ">" ) ) changed |= Move( sequence, 1 );
    ImGui::SetItemTooltip( "Move selected keys one frame right" );
    ImGui::SameLine();
    if( ImGui::Button( "Delete" ) ) changed |= Delete( sequence );
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
    ImGui::SetItemTooltip( "Box select: drag empty space\nAdd selection: Shift\nToggle key: Ctrl/Cmd-click\nMove selection: drag a selected key\nZoom: Ctrl/Cmd + wheel\nPan: middle drag\nCancel drag: Escape" );
    if( !Message.empty() ) ImGui::TextColored( ImGuiTheme::WarningText(), "%s", Message.c_str() );

    ImGui::PushStyleColor( ImGuiCol_ChildBg, ImGuiTheme::ActivePalette().Recessed );
    ImGui::BeginChild( "Sequence Canvas", ImVec2( 0.f, 0.f ), ImGuiChildFlags_Border );
    ImGui::PopStyleColor();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = std::max( ImGui::GetContentRegionAvail().x, ImGuiTheme::Em( 6.f ) );
    const float rowHeight = ImGuiTheme::Em( 2.9f );
    const float header = ImGuiTheme::Em( 2.1f );
    const float legend = std::clamp( width * 0.3f, ImGuiTheme::Em( 8.f ), ImGuiTheme::Em( 15.f ) );
    const float grid = origin.x + legend;
    const float gridWidth = std::max( width - legend - 10.f, 20.f );
    m_gridOrigin = ImVec2( grid, origin.y + header );
    m_rowHeight = rowHeight;
    const float height = std::max( header + rowHeight * sequence.Tracks.size(), ImGui::GetContentRegionAvail().y );
    if( m_fit || m_pixelsPerFrame <= 0.f )
    {
        m_pixelsPerFrame = std::clamp( gridWidth / std::max( sequence.DurationFrames, 1 ), 0.05f, 80.f );
        m_firstFrame = 0.f;
        m_fit = false;
    }
    ImGui::InvisibleButton( "##keys", ImVec2( width, height ), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle );
    // Entering runtime can disable the canvas in the middle of a pointer gesture.
    if( ImGui::GetItemFlags() & ImGuiItemFlags_Disabled ) m_drag = m_box = m_scrub = false;
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImGuiIO& io = ImGui::GetIO();
    const bool control = io.KeyCtrl || io.KeySuper;
    auto toFrame = [&]( float x ) { return m_firstFrame + ( x - grid ) / m_pixelsPerFrame; };
    auto toRow = [&]( float y ) { return static_cast<int>( std::floor( ( y - origin.y - header ) / rowHeight ) ); };
    auto keyX = [&]( float f ) { return grid + ( f - m_firstFrame ) * m_pixelsPerFrame; };
    auto keyY = [&]( int row ) { return origin.y + header + ( row + 0.5f ) * rowHeight; };

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
    if( mouseRow >= 0 && mouseRow < static_cast<int>( sequence.Tracks.size() ) && io.MousePos.x >= grid )
        for( const auto& key : sequence.Tracks[mouseRow].Keys )
            if( std::abs( io.MousePos.x - keyX( key.Frame ) ) <= hitRadius && std::abs( io.MousePos.y - keyY( mouseRow ) ) <= hitRadius ) { hit = { static_cast<size_t>( mouseRow ), key.Frame }; hasHit = true; break; }

    if( hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
    {
        Message.clear();
        m_start = io.MousePos;
        if( io.MousePos.x < grid )
        {
            if( mouseRow >= 0 && mouseRow < static_cast<int>( sequence.Tracks.size() ) ) track = mouseRow;
        }
        else if( io.MousePos.y < origin.y + header ) m_scrub = true;
        else if( hasHit )
        {
            track = static_cast<int>( hit.Track );
            const bool selected = SequenceEditing::Contains( Selected, hit );
            if( control && selected ) Selected.erase( std::remove( Selected.begin(), Selected.end(), hit ), Selected.end() );
            else
            {
                if( !selected && !control && !io.KeyShift ) Selected.clear();
                if( !selected ) Selected.push_back( hit );
                m_drag = true;
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
    if( m_drag && ImGui::IsMouseDragging( ImGuiMouseButton_Left ) )
    {
        int delta = static_cast<int>( std::lround( ( io.MousePos.x - m_start.x ) / m_pixelsPerFrame / m_snap ) ) * m_snap;
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
        Selected = m_originalSelection;
        m_drag = m_box = m_scrub = false;
    }
    if( !io.MouseDown[0] ) m_drag = m_box = m_scrub = false;

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
    for( size_t row = 0; row < sequence.Tracks.size(); ++row )
    {
        const float y = origin.y + header + row * rowHeight;
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
        draw->PushClipRect( ImVec2( origin.x, y ), ImVec2( grid - inset, y + rowHeight ), true );
        draw->AddText( ImVec2( origin.x + inset, y + ImGuiTheme::Em( 0.35f ) ), ImGui::GetColorU32( lane.Enabled ? palette.Text : palette.Muted ), binding.c_str() );
        draw->AddText( ImVec2( origin.x + inset, y + ImGuiTheme::Em( 1.55f ) ), ImGui::GetColorU32( palette.Muted ), property.c_str() );
        draw->PopClipRect();
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
    if( hovered && hasHit && !m_drag && !m_box )
    {
        ImGui::SetTooltip( "%s.%s\nFrame %d  |  %.2f s", sequence.Tracks[hit.Track].Component.c_str(), sequence.Tracks[hit.Track].Property.c_str(), hit.Frame, float( hit.Frame ) / sequence.FrameRate );
        ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeEW );
    }
    if( sequence.Tracks.empty() )
        draw->AddText( ImVec2( grid + inset, origin.y + header + inset ), ImGui::GetColorU32( palette.Muted ), "Add a track to begin animating" );
    ImGui::EndChild();
    return changed;
}
#endif
