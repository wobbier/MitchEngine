#include "TerrainWidget.h"
#if USING( ME_EDITOR )
#include "Editor/EditorActions.h"
#include "Editor/EditorOperations.h"
#include "Editor/SceneTools.h"
#include "Editor/Selection.h"
#include "EditorApp.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/Graphics/Terrain.h"
#include "Resource/AssetDatabase.h"
#include "Components/Transform.h"
#include "Events/EditorEvents.h"
#include "Widgets/SceneViewWidget.h"
#include "Utils/EditorConfig.h"
#include "Utils/HavanaUtils.h"
#include "Utils/ImGuiUtils.h"
#include "ImGuiTheme.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <algorithm>
#include <cmath>
#include <fstream>

namespace
{
    TerrainWidget* s_terrainWidget = nullptr;
    const char* kCategories[] = { "Sculpt", "Paint", "Tiles", "Foliage", "Splines", "Inspect" };
    const char* kCategoryIcons[] = { "mountain", "paint", "layout-grid", "plant", "route", "eye" };
    const char* kToolNames[] = { "Raise", "Lower", "Smooth", "Flatten" };
    const char* kToolIcons[] = { "arrow-up", "arrow-down", "blur", "equal" };
    std::string Key( const std::string& path ) { return std::filesystem::path( Path( path ).FullPath ).lexically_normal().generic_string(); }
    void RawControls( TerrainData::RawHeightmap& format, const TerrainData::Settings* settings = nullptr )
    {
        int encoding = format.Encoding == TerrainData::HeightEncoding::UInt16 ? 0 : 1;
        if( ImGui::Combo( "Encoding", &encoding, "UInt16 (normalized)\0Float32\0" ) )
        {
            format.Encoding = encoding ? TerrainData::HeightEncoding::Float32 : TerrainData::HeightEncoding::UInt16;
            format.Offset = encoding ? 0 : ( settings ? settings->HeightBase : -512 );
            format.Scale = encoding ? 1 : ( settings ? settings->HeightRange : 1024 );
        }
        int order = format.BigEndian ? 1 : 0;
        if( ImGui::Combo( "Byte order", &order, "Little endian\0Big endian\0" ) ) format.BigEndian = order != 0;
        ImGui::Checkbox( "Flip Z rows", &format.FlipZ );
        ImGui::InputDouble( "Height offset (m)", &format.Offset, 0, 0, "%.6g" );
        ImGui::InputDouble( "Height scale", &format.Scale, 0, 0, "%.6g" );
        ImGui::Text( "%u x %u samples (outer border included)", format.Width, format.Height );
        ImGui::TextDisabled( "Heights outside the terrain range are rejected." );
        if( format.Encoding == TerrainData::HeightEncoding::UInt16 )
            ImGui::TextDisabled( "UInt16 quantization: at most %.6g m + Float32 rounding", std::abs( format.Scale ) / 131070 );
    }
}

TerrainWidget::TerrainWidget() : HavanaWidget( "Terrain" )
{
    s_terrainWidget = this; IsOpen = false;
    Listen<OpenTerrainEvent>( [this]( const OpenTerrainEvent& event ) { Open( event.AssetPath ); return true; } );
}
TerrainWidget::~TerrainWidget() { if( s_terrainWidget == this ) s_terrainWidget = nullptr; }
TerrainWidget* TerrainWidget::Get() { return s_terrainWidget; }
bool TerrainWidget::IsPlaying() const { return static_cast<EditorApp*>( GetEngine().GetGame() )->IsGameRunning(); }

void TerrainWidget::Init()
{
    m_rawImport.Offset = -512; m_rawImport.Scale = 1024;
    auto& config = EditorConfig::GetInstance();
    Brush.Radius = std::clamp( config.GetPreference<float>( "Terrain.Radius", 4.f ), 0.25f, 128.f );
    Brush.Strength = std::clamp( config.GetPreference<float>( "Terrain.Strength", 0.5f ), 0.001f, 100.f );
    Brush.Falloff = std::clamp( config.GetPreference<float>( "Terrain.Falloff", 0.65f ), 0.f, 1.f );
    Brush.Spacing = std::clamp( config.GetPreference<float>( "Terrain.Spacing", 0.2f ), 0.05f, 1.f );
    auto& actions = EditorActions::Get();
    actions.Register( { "View.Terrain", "Terrain", "View", 0, 0, [this]() { IsOpen = true; m_focusRequested = true; } } );
    actions.Register( { "Terrain.New", "New Terrain...", "Terrain", 0, 0, [this]() { NewDialog(); }, [this]() { return !IsPlaying(); } } );
    actions.Register( { "Terrain.Import", "Import Terrain Heightmap...", "Terrain", 0, 0, [this]() { ImportDialog(); }, [this]() { return !IsPlaying(); } } );
    actions.Register( { "Terrain.Export", "Export Terrain Heightmap...", "Terrain", 0, 0, [this]() { ExportDialog(); }, [this]() { return m_active && !IsPlaying(); } } );
    actions.Register( { "Terrain.MakeUnique", "Make Terrain Unique...", "Terrain", 0, 0, [this]() { MakeUniqueDialog(); }, [this]() {
        auto selected = Selection::Get().GetActive(); auto* terrain = selected ? selected->TryGetComponent<Terrain>() : nullptr;
        return m_active && terrain && !IsPlaying() && Key( terrain->Asset.GetLocalPathString() ) == Key( m_active->GetPath() );
    } } );
    actions.Register( { "Terrain.Open", "Open Terrain...", "Terrain", 0, 0, [this]() {
        RequestAssetSelectionEvent event( [this]( Path path ) { Open( path.GetLocalPathString() ); }, AssetType::Terrain ); event.Fire();
    } } );
    actions.Register( { "Terrain.SaveAll", "Save All Terrains", "Terrain", 0, 0, [this]() { SaveAll(); }, [this]() { return !IsPlaying() && HasUnsaved(); } } );
    actions.Register( { "File.SaveAll", "Save All", "File", 0, 0, [this]() {
        if( SaveAll() ) static_cast<EditorApp*>( GetEngine().GetGame() )->SaveScene( false );
    }, [this]() { return !IsPlaying(); } } );
    actions.Register( { "Terrain.StopBrush", "Stop Terrain Brush", "Terrain", 0, 0, [this]() { if( m_active ) m_active->EndStroke(); BrushEnabled = false; } } );
    InitRecovery();
}

void TerrainWidget::InstallActionRouting()
{
    auto& actions = EditorActions::Get();
    auto previousClaims = actions.DocumentClaims;
    auto previousExecute = actions.DocumentExecute;
    actions.DocumentClaims = [this, previousClaims]( const std::string& id ) { return ClaimsAction( id ) || ( previousClaims && previousClaims( id ) ); };
    actions.DocumentExecute = [this, previousExecute]( const std::string& id ) {
        if( ClaimsAction( id ) )
        {
            if( m_active ) m_active->EndStroke();
            if( id == "File.Save" ) m_active->Save();
            else if( id == "Edit.Undo" ) GetHistory().Undo();
            else if( id == "Edit.Redo" ) GetHistory().Redo();
        }
        else if( previousExecute ) previousExecute( id );
    };
}

bool TerrainWidget::ClaimsAction( const std::string& id ) const
{
    if( !IsOpen ) return false;
    return !IsPlaying() && ( m_focused || m_viewportOwnsActions || ( m_active && m_active->IsStroking() ) ) &&
        ( ( m_active && id == "File.Save" ) || id == "Edit.Undo" || id == "Edit.Redo" );
}

void TerrainWidget::Destroy()
{
    for( auto& document : m_documents ) document->EndStroke();
    if( AutomaticRecoveryEnabled() ) WriteRecoveries();
    m_active = nullptr; m_documents.clear();
    m_recoveryDirtyDocuments.clear();
    for( const auto& [name, handle] : m_icons ) if( bgfx::isValid( handle ) ) bgfx::destroy( handle );
    m_icons.clear();
}

TerrainDocument* TerrainWidget::Open( const std::string& requested )
{
    std::string path = requested;
    if( !std::filesystem::exists( Path( path ).FullPath ) )
    {
        const auto moved = AssetDatabase::Get().FindMovedPath( Path( path ).GetLocalPathString() );
        if( !moved.empty() ) path = moved;
    }
    if( Path( path ).GetExtension() != std::string_view( "terrain" ) )
    { m_error = "Open the source .terrain asset to author heights. Cooked terrain is read-only."; IsOpen = true; return nullptr; }
    for( auto& document : m_documents ) if( Key( document->GetPath() ) == Key( path ) )
    {
        if( m_active ) m_active->EndStroke();
        m_active = document.get(); IsOpen = true; m_focusRequested = true; return m_active;
    }
    auto document = std::make_unique<TerrainDocument>( path );
    if( !document->Load() ) { m_error = document->GetError(); IsOpen = true; return nullptr; }
    if( m_active ) m_active->EndStroke();
    m_active = document.get(); m_documents.push_back( std::move( document ) );
    IsOpen = true; m_focusRequested = true; m_error.clear();
    return m_active;
}

TerrainDocument* TerrainWidget::Create( const std::string& path, const TerrainData::Settings& settings, float height )
{
    if( IsPlaying() ) return nullptr;
    auto document = std::make_unique<TerrainDocument>( path );
    if( !document->CreateFlat( settings, height ) ) { m_error = document->GetError(); IsOpen = true; return nullptr; }
    if( m_active ) m_active->EndStroke();
    m_active = document.get(); m_documents.push_back( std::move( document ) );
    IsOpen = true; m_focusRequested = true; m_error.clear();
    return m_active;
}

TerrainDocument* TerrainWidget::CreateFromField( const std::string& path, TerrainData::Heightfield field )
{
    if( IsPlaying() ) return nullptr;
    auto document = std::make_unique<TerrainDocument>( path );
    if( !document->CreateFromField( std::move( field ) ) ) { m_error = document->GetError(); IsOpen = true; return nullptr; }
    if( m_active ) m_active->EndStroke();
    m_active = document.get(); m_documents.push_back( std::move( document ) );
    IsOpen = true; m_focusRequested = true; m_error.clear();
    return m_active;
}

bool TerrainWidget::MakeUnique( const std::string& path, uint64_t entityGUID )
{
    auto entity = entityGUID ? EditorOps::GetWorld().FindEntityByGUID( entityGUID ) : Selection::Get().GetActive();
    auto* terrain = entity ? entity->TryGetComponent<Terrain>() : nullptr;
    if( IsPlaying() || !m_active || !terrain || Key( terrain->Asset.GetLocalPathString() ) != Key( m_active->GetPath() ) )
    { m_error = "Select an instance of the active terrain to make it unique"; return false; }
    if( !m_active->SaveCopy( path ) ) { m_error = m_active->GetError(); return false; }
    // Load the copy before changing the scene reference. Failure leaves the scene untouched.
    if( !Open( path ) ) return false;
    const auto before = EditorOps::CaptureComponent( *entity.Get(), "Terrain" );
    terrain->Asset = Path( path );
    EditorOps::RecordComponentEdit( *entity.Get(), "Terrain", before, EditorOps::CaptureComponent( *entity.Get(), "Terrain" ), "Make terrain unique" );
    return true;
}

bool TerrainWidget::HasUnsaved() const
{
    for( const auto& document : m_documents ) if( document->IsDirty() ) return true;
    return false;
}
bool TerrainWidget::SaveAll()
{
    if( IsPlaying() ) return false;
    for( auto& document : m_documents ) if( document->IsDirty() && !document->Save() )
    { m_error = document->GetError(); IsOpen = true; return false; }
    return true;
}
void TerrainWidget::ConfirmUnsaved( std::function<void()> action ) { m_pending = std::move( action ); m_confirmRequested = true; }
void TerrainWidget::NewDialog() { IsOpen = true; m_newRequested = true; m_importHeightmap = false; }
void TerrainWidget::ImportDialog() { NewDialog(); m_importHeightmap = true; }
void TerrainWidget::ExportDialog()
{
    if( !m_active || IsPlaying() ) return;
    m_active->EndStroke(); m_exportRequested = true;
    const auto& s = m_active->GetField().GetSettings();
    m_rawExport.Width = s.TilesX * s.TileQuads + 1; m_rawExport.Height = s.TilesZ * s.TileQuads + 1;
    m_rawExport.Offset = m_rawExport.Encoding == TerrainData::HeightEncoding::UInt16 ? s.HeightBase : 0;
    m_rawExport.Scale = m_rawExport.Encoding == TerrainData::HeightEncoding::UInt16 ? s.HeightRange : 1;
}
void TerrainWidget::MakeUniqueDialog()
{
    auto entity = Selection::Get().GetActive();
    if( !entity || !m_active || IsPlaying() ) return;
    const uint64_t guid = entity->GetGUID();
    RequestAssetSelectionEvent event( [this, guid]( Path path ) { MakeUnique( path.GetLocalPathString(), guid ); }, AssetType::Terrain, true ); event.Fire();
}

bgfx::TextureHandle TerrainWidget::Icon( const char* name )
{
    auto found = m_icons.find( name );
    if( found != m_icons.end() ) return found->second;
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    std::ifstream file( Path( std::string( "Assets/Icons/outline/" ) + name + ".svg" ).FullPath );
    if( file )
    {
        std::string svg( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
        size_t pos = 0;
        while( ( pos = svg.find( "currentColor", pos ) ) != std::string::npos ) { svg.replace( pos, 12, "#ffffff" ); pos += 7; }
        static bx::DefaultAllocator allocator;
        if( auto* image = bimg::imageParseSvg( &allocator, svg.data(), static_cast<uint32_t>( svg.size() ), 64, 64 ) )
        {
            handle = bgfx::createTexture2D( static_cast<uint16_t>( image->m_width ), static_cast<uint16_t>( image->m_height ), false, 1,
                bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy( image->m_data, image->m_size ) );
            bimg::imageFree( image );
        }
    }
    m_icons.emplace( name, handle );
    return handle;
}

bool TerrainWidget::Button( const char* icon, const char* label, const char* description, bool enabled, bool selected, bool compact )
{
    ImGui::PushID( label );
    ImGui::BeginDisabled( !enabled );
    if( selected ) ImGui::PushStyleColor( ImGuiCol_Button, ImGui::GetStyleColorVec4( ImGuiCol_ButtonActive ) );
    // Tables set an explicit width for each full-size tile.
    const float tileWidth = compact ? ImGui::GetFrameHeight() : ImGui::GetContentRegionAvail().x;
    const ImVec2 size( tileWidth, compact ? tileWidth : ImGuiTheme::Em( 3.55f ) );
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::Button( "##tool", size );
    if( selected ) ImGui::PopStyleColor();
    const float iconSize = compact ? ImGuiTheme::Em( 1.05f ) : ImGuiTheme::Em( 1.5f );
    const float iconY = start.y + ( compact ? ( size.y - iconSize ) * 0.5f : ImGui::GetStyle().FramePadding.y );
    const auto texture = Icon( icon );
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::GetColorU32( ImGuiCol_Text );
    if( bgfx::isValid( texture ) )
        draw->AddImage( ImGui::toId( texture, IMGUI_FLAGS_ALPHA_BLEND, 0 ), ImVec2( start.x + ( size.x - iconSize ) * 0.5f, iconY ),
            ImVec2( start.x + ( size.x + iconSize ) * 0.5f, iconY + iconSize ), ImVec2( 0, 0 ), ImVec2( 1, 1 ), color );
    else draw->AddText( ImVec2( start.x + size.x * 0.4f, iconY ), color, "?" );
    if( !compact )
    {
        const float textWidth = ImGui::CalcTextSize( label ).x;
        draw->PushClipRect( start, ImVec2( start.x + size.x, start.y + size.y ), true );
        draw->AddText( ImVec2( start.x + std::max( 0.f, ( size.x - textWidth ) * 0.5f ), start.y + size.y - ImGui::GetTextLineHeight() - ImGui::GetStyle().FramePadding.y ), color, label );
        draw->PopClipRect();
    }
    ImGui::EndDisabled();
    if( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
    {
        ImGui::BeginTooltip(); ImGui::TextUnformatted( label );
        ImGui::PushTextWrapPos( ImGuiTheme::Em( 22 ) ); ImGui::TextUnformatted( description ); ImGui::PopTextWrapPos(); ImGui::EndTooltip();
    }
    ImGui::PopID();
    return clicked;
}
void TerrainWidget::Planned( const char* icon, const char* label, const char* description )
{
    ImGui::TableNextColumn();
    Button( icon, label, ( std::string( "Planned: " ) + description ).c_str(), false );
}

void TerrainWidget::DrawTools()
{
    ImGui::SeparatorText( kCategories[m_category] );
    if( ImGui::BeginTable( "Tool shelf", 3, ImGuiTableFlags_SizingStretchSame ) )
    {
        if( m_category == 0 )
        {
            for( int i = 0; i < 4; ++i )
            {
                ImGui::TableNextColumn();
                if( Button( kToolIcons[i], kToolNames[i], i == 3 ? "Flatten toward the height picked at the start of the stroke." : "Drag over the selected terrain in World View. Shift reverses Raise / Lower.", !IsPlaying(), BrushEnabled && static_cast<int>( Brush.Tool ) == i ) )
                { if( m_active ) m_active->EndStroke(); Brush.Tool = static_cast<TerrainData::BrushTool>( i ); BrushEnabled = true; }
            }
            Planned( "wave-sine", "Noise", "Add procedural height variation." );
            Planned( "photo", "Stamp", "Sculpt with a height stamp or brush alpha." );
            Planned( "droplet", "Erosion", "Simulate hydraulic erosion." );
            Planned( "mountain-off", "Thermal", "Relax slopes with thermal erosion." );
            Planned( "stairs", "Terrace", "Create stepped elevation bands." );
            Planned( "ripple", "Ramp", "Build a slope between two picked heights." );
            Planned( "scissors", "Holes", "Cut holes for caves and entrances." );
            Planned( "mask", "Mask", "Protect regions from sculpting." );
        }
        else if( m_category == 1 )
        {
            Planned( "brush", "Paint", "Paint terrain material weights." );
            Planned( "eraser", "Erase", "Remove weights from the active layer." );
            Planned( "blur", "Blend", "Smooth transitions between material layers." );
            Planned( "bucket", "Fill", "Fill a tile or selection with a layer." );
            Planned( "color-swatch", "Layers", "Create and reorder terrain material layers." );
            Planned( "plus", "Add Layer", "Add a surface material to the terrain palette." );
            Planned( "ruler", "Slope", "Paint using slope limits." );
            Planned( "mountain", "Altitude", "Paint within an elevation band." );
            Planned( "mask", "Masks", "Combine procedural painting masks." );
            Planned( "sphere", "Material", "Edit terrain surface shading and tiling." );
            Planned( "upload", "Import", "Import weight maps without color conversion." );
            Planned( "download", "Export", "Export terrain layer weight maps." );
        }
        else if( m_category == 2 )
        {
            Planned( "plus", "Add Tile", "Grow terrain with consistent shared borders." );
            Planned( "minus", "Remove", "Remove a tile and update its neighbors." );
            Planned( "arrows-move", "Move", "Reposition terrain tiles on the source grid." );
            Planned( "copy", "Duplicate", "Duplicate tiles and resolve shared borders." );
            Planned( "border-all", "Stitch", "Inspect and repair imported page borders." );
            Planned( "arrows-maximize", "Resize", "Change terrain extents and sample density." );
            ImGui::TableNextColumn(); if( Button( "upload", "Heightmap", "Create a terrain from RAW Float32 or UInt16 heights.", !IsPlaying() ) ) ImportDialog();
            ImGui::TableNextColumn(); if( Button( "download", "Export", "Export this terrain's canonical samples as a RAW heightmap.", m_active && !IsPlaying() ) ) ExportDialog();
            Planned( "map-2", "Overview", "Navigate a large tiled terrain map." );
            Planned( "hand-stop", "Pin", "Keep selected source pages resident." );
            Planned( "refresh", "Stream", "Preview tile residency and streaming budgets." );
            Planned( "package", "Cook", "Publish optimized runtime terrain data." );
        }
        else if( m_category == 3 )
        {
            Planned( "tree", "Trees", "Paint instanced trees with species and density settings." );
            Planned( "plant", "Grass", "Paint grass and ground cover." );
            Planned( "flower", "Details", "Scatter rocks, flowers and small props." );
            Planned( "eraser", "Erase", "Remove foliage within the brush." );
            Planned( "grid-dots", "Density", "Paint a foliage density mask." );
            Planned( "ruler", "Scale", "Set instance scale and rotation ranges." );
            Planned( "mask", "Exclusion", "Keep vegetation away from paths and buildings." );
            Planned( "refresh", "Reseed", "Regenerate deterministic placement." );
            Planned( "eye", "Visibility", "Preview foliage culling and draw distances." );
        }
        else if( m_category == 4 )
        {
            Planned( "road", "Road", "Author a terrain-conforming road spline." );
            Planned( "ripple", "River", "Carve river beds and shorelines." );
            Planned( "route", "Path", "Lay trails and material strips." );
            Planned( "vector-bezier", "Points", "Add, move and remove spline control points." );
            Planned( "ruler", "Width", "Vary the spline corridor width." );
            Planned( "magnet", "Conform", "Fit heights along a spline." );
            Planned( "blur", "Shoulder", "Blend spline edges into surrounding terrain." );
            Planned( "paint", "Surface", "Paint a material along the spline." );
            Planned( "package", "Bake", "Bake spline edits into source pages." );
        }
        else
        {
            Planned( "layout-grid", "Patches", "Display render patch boundaries." );
            Planned( "chart-area-line", "LODs", "Inspect selected patch LODs and error." );
            Planned( "border-all", "Seams", "Highlight shared tile edges." );
            Planned( "mountain", "Heights", "Visualize source elevations." );
            Planned( "color-swatch", "Weights", "Inspect material layer weights." );
            Planned( "sphere", "Normals", "Visualize heightfield normals." );
            Planned( "world", "Collision", "Compare static collision with the authoring surface." );
            Planned( "grid-dots", "Navmesh", "Inspect navigation rebuild status." );
            Planned( "refresh", "Residency", "Inspect loaded, pending and evicted tiles." );
            Planned( "search", "Validate", "Check source generations, borders and derived data." );
            Planned( "settings", "Budgets", "Tune mesh, residency and history budgets." );
            Planned( "tools", "Repair", "Repair or compact terrain asset data." );
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled( "Dimmed tools are planned. Hover for details." );
}

void TerrainWidget::DrawBrush()
{
    ImGui::SeparatorText( "Brush" );
    if( Button( BrushEnabled ? "hand-stop" : "pointer", BrushEnabled ? "Stop sculpting" : "Enable brush", "Enable or release terrain input in World View.", m_active && !IsPlaying(), BrushEnabled, true ) )
    { if( m_active ) m_active->EndStroke(); BrushEnabled = !BrushEnabled; }
    ImGui::SameLine(); ImGui::Text( "%s  /  %s", kToolNames[static_cast<int>( Brush.Tool )], BrushEnabled ? "Active" : "Idle" );
    const float spacing = m_active ? m_active->GetField().GetSettings().SampleSpacing : 1.f;
    ImGui::BeginDisabled( IsPlaying() || ( m_active && m_active->IsStroking() ) );
    auto control = [&]( BrushParameter parameter, const char* label, const char* id, float current, float speed, float min, float max, const char* format, bool slider ) {
        HavanaUtils::Label( label );
        float value = current;
        const bool changed = slider ? ImGui::SliderFloat( id, &value, min, max, format ) : ImGui::DragFloat( id, &value, speed, min, max, format, ImGuiSliderFlags_AlwaysClamp );
        const auto rectMin = ImGui::GetItemRectMin(), rectMax = ImGui::GetItemRectMax();
        m_brushCenters[static_cast<int>( parameter )] = Vector2( ( rectMin.x + rectMax.x ) * 0.5f, ( rectMin.y + rectMax.y ) * 0.5f );
        if( ImGui::IsItemActivated() ) m_brushBefore = current;
        if( changed ) SetBrushParameter( parameter, value, false );
        if( ImGui::IsItemDeactivatedAfterEdit() ) RecordBrushParameter( parameter, m_brushBefore );
    };
    control( BrushParameter::Radius, "Radius", "##radius", Brush.Radius, spacing * 0.1f, spacing * 0.25f, spacing * 128, "%.2f m", false );
    control( BrushParameter::Strength, "Strength", "##strength", Brush.Strength, 0.01f, 0.001f, 100.f, static_cast<int>( Brush.Tool ) >= 2 ? "%.3f blend" : "%.3f m / stamp", false );
    control( BrushParameter::Falloff, "Falloff", "##falloff", Brush.Falloff, 0, 0, 1, "%.2f", true );
    control( BrushParameter::Spacing, "Spacing", "##spacing", Brush.Spacing, 0, 0.05f, 1, "%.2f radii", true );
    ImGui::EndDisabled();
    ImGui::TextDisabled( "Drag to sculpt  /  Shift reverses raise & lower" );
    ImGui::TextDisabled( "Esc cancels stroke  /  RMB navigates" );
}

void TerrainWidget::Render()
{
    UpdateRecovery();
    DrawRecovery();
    DrawModals();
    if( m_active && m_active->IsStroking() )
    {
        if( ImGui::GetIO().AppFocusLost || ImGui::IsKeyPressed( ImGuiKey_Escape ) ) m_active->EndStroke( true );
        else if( IsPlaying() || !ImGui::IsMouseDown( ImGuiMouseButton_Left ) ) m_active->EndStroke();
    }
    if( !IsOpen ) { if( m_active ) m_active->EndStroke(); BrushEnabled = false; m_focused = false; return; }
    if( m_focusRequested ) { ImGui::SetNextWindowFocus(); m_focusRequested = false; }
    ImGui::SetNextWindowSize( ImVec2( ImGuiTheme::Em( 25 ), ImGuiTheme::Em( 52 ) ), ImGuiCond_FirstUseEver );
    if( !ImGui::Begin( Name.c_str(), &IsOpen, m_active && m_active->IsDirty() ? ImGuiWindowFlags_UnsavedDocument : 0 ) )
    { m_focused = false; ImGui::End(); return; }
    const ImGuiWindow* nav = ImGui::GetCurrentContext()->NavWindow;
    const bool transient = nav && ( nav->Flags & ( ImGuiWindowFlags_Popup | ImGuiWindowFlags_MenuBar ) );
    if( !transient ) m_focused = ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
    const bool editing = !IsPlaying();
    if( Button( "file-plus", "New Terrain", "Create a flat source terrain asset.", editing, false, true ) ) NewDialog();
    ImGui::SameLine( 0, ImGuiTheme::InnerGap() );
    if( Button( "folder-open", "Open Terrain", "Open an existing source terrain asset.", true, false, true ) ) EditorActions::Get().Execute( "Terrain.Open" );
    ImGui::SameLine( 0, ImGuiTheme::InnerGap() );
    if( Button( "upload", "Import Heightmap", "Create terrain from a RAW UInt16 or Float32 heightmap.", editing, false, true ) ) ImportDialog();
    ImGui::SameLine( 0, ImGuiTheme::InnerGap() );
    if( Button( "download", "Export Heightmap", "Export this terrain's source heights.", editing && m_active, false, true ) ) ExportDialog();
    ImGui::SameLine( 0, ImGuiTheme::InnerGap() );
    if( Button( "device-floppy", "Save Terrain", "Save this terrain asset. Scene references are saved separately.", editing && m_active && m_active->IsDirty(), false, true ) ) m_active->Save();
    ImGui::SameLine( 0, ImGuiTheme::InnerGap() );
    if( Button( "copy", "Save Copy", "Save the current heights as a new independent asset.", editing && m_active, false, true ) )
    {
        const uint64_t sourceGUID = m_active->GetGUID();
        RequestAssetSelectionEvent event( [this, sourceGUID]( Path path ) {
            for( auto& source : m_documents ) if( source->GetGUID() == sourceGUID )
            { if( !source->SaveCopy( path.GetLocalPathString() ) ) m_error = source->GetError(); return; }
            m_error = "The terrain document was closed. Reopen it before saving a copy.";
        }, AssetType::Terrain, true ); event.Fire();
    }
    ImGui::SameLine( 0, ImGuiTheme::InnerGap() );
    if( Button( "arrow-back-up", "Undo Terrain", "Undo the latest terrain stroke or brush setting.", editing && GetHistory().CanUndo(), false, true ) ) { if( m_active ) m_active->EndStroke(); GetHistory().Undo(); }
    ImGui::SameLine( 0, ImGuiTheme::InnerGap() );
    if( Button( "arrow-forward-up", "Redo Terrain", "Redo the latest terrain stroke or brush setting.", editing && GetHistory().CanRedo(), false, true ) ) { if( m_active ) m_active->EndStroke(); GetHistory().Redo(); }
    ImGuiTheme::SectionGap();
    if( m_active )
    {
        const std::string title = std::filesystem::path( m_active->GetPath() ).stem().string() + ( m_active->IsDirty() ? " *" : "" );
        ImGui::SetNextItemWidth( -1 );
        if( ImGui::BeginCombo( "##document", title.c_str() ) )
        {
            for( auto& document : m_documents ) if( ImGui::Selectable( ( document->GetPath() + ( document->IsDirty() ? " *" : "" ) ).c_str(), document.get() == m_active ) )
            { m_active->EndStroke(); m_active = document.get(); }
            ImGui::EndCombo();
        }
        const auto& s = m_active->GetField().GetSettings();
        ImGui::TextDisabled( "%u x %u tiles  /  %.0f x %.0f m", s.TilesX, s.TilesZ, s.TilesX * s.TileQuads * s.SampleSpacing, s.TilesZ * s.TileQuads * s.SampleSpacing );
        if( ImGui::IsItemHovered() ) ImGui::SetTooltip( "%s", m_active->GetPath().c_str() );
        if( const auto& preview = m_active->GetPreview() )
            ImGui::TextDisabled( "Last preview: %zu / %zu patches, %zu / %zu collision tiles rebuilt", preview->BuiltPatches, preview->Patches.size(), preview->BuiltCollisionTiles, preview->CollisionTiles.size() );
        ImGui::BeginDisabled( !editing );
        if( ImGui::Button( "Place in Scene", ImVec2( -1, 0 ) ) ) PlaceInScene();
        ImGui::EndDisabled();
    }
    else ImGui::TextWrapped( "Create a terrain or open a .terrain asset to start authoring." );
    if( !m_error.empty() ) { ImGui::TextColored( ImGuiTheme::ErrorText(), "%s", m_error.c_str() ); }
    if( m_active && !m_active->GetError().empty() ) ImGui::TextWrapped( "%s", m_active->GetError().c_str() );
    if( !editing ) ImGui::TextColored( ImGuiTheme::WarningText(), "Stop Play to edit terrain." );
    ImGuiTheme::SectionGap();
    if( ImGui::BeginTable( "Modes", 3, ImGuiTableFlags_SizingStretchSame ) )
    {
        for( int i = 0; i < 6; ++i )
        {
            ImGui::TableNextColumn();
            if( Button( kCategoryIcons[i], kCategories[i], i ? "Browse the planned tools in this workspace." : "Sculpt source heights in World View.", true, m_category == i ) ) m_category = i;
        }
        ImGui::EndTable();
    }
    DrawTools();
    if( m_category == 0 ) DrawBrush();
    if( ImGui::CollapsingHeader( "Asset & Build" ) )
    {
        if( ImGui::Button( "Save All Terrains", ImVec2( -1, 0 ) ) && editing ) SaveAll();
        if( ImGui::Button( "Revert from Disk...", ImVec2( -1, 0 ) ) && m_active && editing )
        {
            // Revert confirmation applies to the active document only.
            m_revertTarget = m_active; ImGui::OpenPopup( "Revert terrain?" );
        }
        if( ImGui::BeginPopupModal( "Revert terrain?", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
        {
            ImGui::TextUnformatted( "Discard this terrain's unsaved sculpting?" );
            if( ImGui::Button( "Revert" ) ) { if( m_revertTarget ) m_revertTarget->Load(); m_revertTarget = nullptr; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(); if( ImGui::Button( "Cancel" ) ) { m_revertTarget = nullptr; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        if( ImGui::BeginTable( "Future asset tools", 3, ImGuiTableFlags_SizingStretchSame ) )
        {
            ImGui::TableNextColumn();
            auto selected = Selection::Get().GetActive();
            auto* selectedTerrain = selected ? selected->TryGetComponent<Terrain>() : nullptr;
            const bool canFork = m_active && selectedTerrain && Key( selectedTerrain->Asset.GetLocalPathString() ) == Key( m_active->GetPath() );
            if( Button( "copy", "Make Unique", "Copy the active heights to a new asset and rebind only the selected instance.", canFork && editing ) ) MakeUniqueDialog();
            ImGui::TableNextColumn();
            if( Button( "device-floppy", "Recovery", "Browse and restore unsaved terrain snapshots. Restoring keeps source conflict checks." ) )
            { ScanRecoveries(); m_showRecoveryRequested = true; }
            Planned( "package", "Build", "Cook heights, collision and material data." );
            Planned( "trash", "Compact", "Remove source generations no longer referenced." );
            Planned( "refresh", "Rebuild", "Rebuild all derived terrain caches." );
            Planned( "settings", "Settings", "Edit source ranges, resolution and streaming configuration." );
            ImGui::EndTable();
        }
        bool autosave = EditorConfig::GetInstance().GetPreference<bool>( "Terrain.Autosave", true );
        if( ImGui::Checkbox( "Autosave terrain", &autosave ) ) EditorConfig::GetInstance().SetPreference( "Terrain.Autosave", autosave );
        const float interval = EditorConfig::GetInstance().GetPreference<float>( "Autosave.IntervalSeconds", 120.f );
        if( autosave && interval > 0 ) ImGui::TextDisabled( "Recovery interval: %.0f seconds", interval );
        ImGui::BeginDisabled( !editing || !HasUnsaved() );
        if( ImGui::Button( "Save Recovery Now", ImVec2( -1, 0 ) ) ) { if( m_active ) m_active->EndStroke(); WriteRecoveries(); }
        ImGui::EndDisabled();
        if( !m_recoveryMessage.empty() ) ImGui::TextWrapped( "%s", m_recoveryMessage.c_str() );
    }
    ImGui::End();
}

void TerrainWidget::PlaceInScene()
{
    if( !m_active || IsPlaying() ) return;
    UndoTransaction transaction( "Place terrain" );
    auto entity = EditorOps::CreateEntity( std::filesystem::path( m_active->GetPath() ).stem().string() );
    EditorOps::AddComponent( *entity.Get(), "Terrain" );
    const auto before = EditorOps::CaptureComponent( *entity.Get(), "Terrain" );
    entity->GetComponent<Terrain>().Asset = Path( m_active->GetPath() );
    EditorOps::RecordComponentEdit( *entity.Get(), "Terrain", before, EditorOps::CaptureComponent( *entity.Get(), "Terrain" ), "Set terrain asset" );
    Selection::Get().Set( entity );
    EditorActions::Get().Execute( "View.FrameSelected" );
}

void TerrainWidget::DrawModals()
{
    if( m_newRequested ) { ImGui::OpenPopup( "New Terrain" ); m_newRequested = false; }
    if( m_exportRequested ) { ImGui::OpenPopup( "Export Heightmap" ); m_exportRequested = false; }
    if( m_confirmRequested ) { ImGui::OpenPopup( "Unsaved Terrains" ); m_confirmRequested = false; }
    if( ImGui::BeginPopupModal( "New Terrain", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
    {
        ImGui::Checkbox( "Import RAW heightmap", &m_importHeightmap );
        ImGui::SetNextItemWidth( ImGuiTheme::Em( 18 ) ); ImGui::InputInt2( "Tiles X / Z", m_tiles );
        const char* sizes[] = { "32", "64", "128", "256" };
        int sizeIndex = m_quads == 32 ? 0 : m_quads == 64 ? 1 : m_quads == 128 ? 2 : 3;
        if( ImGui::Combo( "Quads per tile", &sizeIndex, sizes, 4 ) ) m_quads = 32 << sizeIndex;
        ImGui::DragFloat( "Sample spacing (m)", &m_spacing, 0.1f, 0.1f, 100.f, "%.2f", ImGuiSliderFlags_AlwaysClamp );
        if( !m_importHeightmap ) ImGui::DragFloat( "Starting height (m)", &m_flatHeight, 0.1f, -512.f, 512.f, "%.2f", ImGuiSliderFlags_AlwaysClamp );
        else
        {
            ImGui::InputText( "RAW file", &m_rawPath );
            if( ImGui::Button( "Browse RAW File..." ) )
            {
                RequestAssetSelectionEvent event( [this]( Path path ) { m_rawPath = path.FullPath; m_newRequested = true; }, AssetType::Unknown ); event.Fire();
                ImGui::CloseCurrentPopup();
            }
            m_rawImport.Width = std::clamp( m_tiles[0], 1, 16 ) * m_quads + 1;
            m_rawImport.Height = std::clamp( m_tiles[1], 1, 16 ) * m_quads + 1;
            RawControls( m_rawImport );
        }
        ImGui::TextDisabled( "Height range: -512 to +512 m. Up to 1M source samples." );
        if( ImGui::Button( "Choose Asset Path..." ) )
        {
            TerrainData::Settings settings; settings.TilesX = std::clamp( m_tiles[0], 1, 16 ); settings.TilesZ = std::clamp( m_tiles[1], 1, 16 );
            settings.TileQuads = m_quads; settings.PatchQuads = std::min( m_quads, 64 ); settings.SampleSpacing = m_spacing;
            auto imported = std::make_shared<TerrainData::Heightfield>();
            bool valid = true;
            if( uint64_t( settings.TilesX ) * settings.TilesZ > 1024 * 1024 / settings.SamplesPerTile() ) { valid = false; m_error = "Choose a terrain with at most 1M stored source samples"; }
            if( valid && m_importHeightmap ) valid = TerrainData::ReadHeightmap( Path( m_rawPath ).FullPath, settings, m_rawImport, *imported, m_error );
            if( valid )
            {
                const bool importing = m_importHeightmap; const float height = m_flatHeight;
                RequestAssetSelectionEvent event( [this, settings, height, importing, imported]( Path path ) {
                    if( importing ) CreateFromField( path.GetLocalPathString(), std::move( *imported ) );
                    else Create( path.GetLocalPathString(), settings, height );
                }, AssetType::Terrain, true ); event.Fire();
                m_error.clear(); ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine(); if( ImGui::Button( "Cancel" ) || ImGui::IsKeyPressed( ImGuiKey_Escape ) ) ImGui::CloseCurrentPopup();
        if( !m_error.empty() ) ImGui::TextWrapped( "%s", m_error.c_str() );
        ImGui::EndPopup();
    }
    if( ImGui::BeginPopupModal( "Export Heightmap", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
    {
        RawControls( m_rawExport, m_active ? &m_active->GetField().GetSettings() : nullptr );
        ImGui::TextDisabled( "Use .r16 / .r32 / .raw. File contains samples only." );
        if( ImGui::Button( "Choose Output File..." ) && m_active )
        {
            const auto format = m_rawExport; TerrainDocument* document = m_active;
            RequestAssetSelectionEvent event( [this, document, format]( Path path ) {
                const auto extension = std::filesystem::path( path.FullPath ).extension().string();
                if( extension != ".r16" && extension != ".r32" && extension != ".raw" && extension != ".f32" && extension != ".h16" && extension != ".bin" )
                { m_error = "Choose a .raw, .r16 or .r32 output file"; return; }
                TerrainData::WriteHeightmap( path.FullPath, document->GetField(), format, m_error );
            }, AssetType::Unknown, true ); event.Fire();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(); if( ImGui::Button( "Cancel" ) || ImGui::IsKeyPressed( ImGuiKey_Escape ) ) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if( ImGui::BeginPopupModal( "Unsaved Terrains", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
    {
        ImGui::TextUnformatted( "Save terrain asset changes before continuing?" );
        for( const auto& document : m_documents ) if( document->IsDirty() ) ImGui::BulletText( "%s", document->GetPath().c_str() );
        std::function<void()> pending;
        if( ImGui::Button( "Save All", ImGuiTheme::DialogButtonSize() ) && SaveAll() ) { pending.swap( m_pending ); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if( ImGui::Button( "Discard", ImGuiTheme::DialogButtonSize() ) )
        {
            bool reverted = true;
            for( auto& document : m_documents ) if( document->IsDirty() && !document->Load() ) { m_error = document->GetError(); reverted = false; }
            if( reverted ) { pending.swap( m_pending ); ImGui::CloseCurrentPopup(); }
        }
        ImGui::SameLine(); if( ImGui::Button( "Cancel", ImGuiTheme::DialogButtonSize() ) ) { m_pending = nullptr; ImGui::CloseCurrentPopup(); }
        if( !m_error.empty() ) ImGui::TextWrapped( "%s", m_error.c_str() );
        ImGui::EndPopup();
        if( pending ) pending();
    }
}

bool TerrainWidget::WantsViewport() const
{
    if( !IsOpen || !BrushEnabled || m_category != 0 || !m_active || IsPlaying() ) return false;
    auto entity = Selection::Get().GetActive();
    if( !entity || !entity->IsActiveInHierarchy() ) return false;
    const auto* terrain = entity->TryGetComponent<Terrain>();
    auto* transform = entity->TryGetComponent<Transform>();
    return terrain && transform && terrain->GetData() && Key( terrain->Asset.GetLocalPathString() ) == Key( m_active->GetPath() ) && TerrainData::IsTranslationOnly( transform->GetLocalToWorldMatrix() );
}

bool TerrainWidget::DrawViewport( SceneViewWidget& view, const Vector2& screenMin, const Vector2& screenSize )
{
    m_viewportOwnsActions = false;
    auto entity = Selection::Get().GetActive();
    if( !WantsViewport() || !view.MainCamera ) { if( m_active ) m_active->EndStroke(); return false; }
    ImGuiIO& io = ImGui::GetIO();
    m_viewportOwnsActions = view.IsFocused;
    if( m_active->IsStroking() && ( entity->GetGUID() != m_strokeEntity || io.AppFocusLost || ImGui::IsKeyPressed( ImGuiKey_Escape ) ) )
    { m_active->EndStroke( true ); return true; }
    if( m_active->IsStroking() && ( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) || !view.IsFocused ) ) m_active->EndStroke();
    if( io.KeyAlt || ImGui::IsMouseDown( ImGuiMouseButton_Right ) || ImGui::IsMouseDown( ImGuiMouseButton_Middle ) || view.IsUsingGuizmo )
    { m_active->EndStroke(); return true; }
    if( !view.IsViewportHovered ) return true;
    const auto translation = entity->GetComponent<Transform>().GetWorldPosition();
    const Ray worldRay = SceneTools::ScreenRay( view.ViewportMousePosition, view.SceneViewRenderSize, view.MainCamera->View, view.MainCamera->ProjectionMatrix );
    Vector3 point;
    if( !TerrainData::RaycastHeightfield( m_active->GetField(), Ray( worldRay.Origin - translation, worldRay.Direction ), point ) ) return true;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect( ImVec2( screenMin.x, screenMin.y ), ImVec2( screenMin.x + screenSize.x, screenMin.y + screenSize.y ), true );
    const Matrix4 vp( view.MainCamera->ProjectionMatrix.GetInternalMatrix() * view.MainCamera->View.GetInternalMatrix() );
    auto ring = [&]( float radius, ImU32 color ) {
        Vector2 previous; bool hasPrevious = false;
        for( int i = 0; i <= 96; ++i )
        {
            const double a = i * 6.28318530718 / 96;
            Vector3 sample( point.x + radius * std::cos( a ), 0, point.z + radius * std::sin( a ) );
            const auto height = m_active->GetField().Query( sample.x, sample.z );
            if( height.Status != TerrainData::QueryStatus::Ready ) { hasPrevious = false; continue; }
            sample.y = height.Height + 0.025f;
            Vector2 pixel;
            if( !SceneTools::WorldToScreen( sample + translation, view.SceneViewRenderSize, vp, pixel ) ) { hasPrevious = false; continue; }
            pixel = screenMin + Vector2( pixel.x * screenSize.x / view.SceneViewRenderSize.x, pixel.y * screenSize.y / view.SceneViewRenderSize.y );
            if( hasPrevious ) { draw->AddLine( ImVec2( previous.x, previous.y ), ImVec2( pixel.x, pixel.y ), IM_COL32( 0, 0, 0, 200 ), 4 ); draw->AddLine( ImVec2( previous.x, previous.y ), ImVec2( pixel.x, pixel.y ), color, 2 ); }
            previous = pixel; hasPrevious = true;
        }
    };
    ring( Brush.Radius, IM_COL32( 255, 215, 90, 255 ) );
    if( Brush.Falloff > 0 && Brush.Falloff < 1 ) ring( Brush.Radius * ( 1 - Brush.Falloff ), IM_COL32( 255, 255, 255, 160 ) );
    draw->PopClipRect();
    if( ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
    {
        ImGui::SetWindowFocus();
        auto brush = Brush;
        const float spacing = m_active->GetField().GetSettings().SampleSpacing;
        brush.Radius = std::clamp( brush.Radius, spacing * 0.25f, spacing * 128 );
        if( io.KeyShift && ( brush.Tool == TerrainData::BrushTool::Raise || brush.Tool == TerrainData::BrushTool::Lower ) )
            brush.Tool = brush.Tool == TerrainData::BrushTool::Raise ? TerrainData::BrushTool::Lower : TerrainData::BrushTool::Raise;
        if( m_active->BeginStroke( brush, point.x, point.z ) ) m_strokeEntity = entity->GetGUID();
    }
    else if( m_active->IsStroking() && !m_active->MoveStroke( point.x, point.z ) ) m_active->EndStroke();
    if( m_active->IsStroking() && ImGui::GetTime() - m_lastPreviewTime >= 0.1 )
    { m_active->PublishPreview(); m_lastPreviewTime = ImGui::GetTime(); }
    return true;
}
#endif
