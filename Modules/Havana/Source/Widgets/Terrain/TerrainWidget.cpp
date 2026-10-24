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
#include "Utils/EditorIcons.h"
#include "Utils/ImGuiUtils.h"
#include "Procedural/Tree/TreeAsset.h"
#include "Procedural/Tree/TreePresets.h"
#include "Widgets/Tree/TreeWidget.h"
#include "ImGuiTheme.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace
{
    TerrainWidget* s_terrainWidget = nullptr;
    const char* kCategories[] = { "Sculpt", "Paint", "Tiles", "Foliage", "Splines", "Inspect", "Trees" };
    const char* kCategoryIcons[] = { "mountain", "paint", "layout-grid", "plant", "route", "eye", "tree" };
    static_assert( std::size( kCategories ) == TerrainWidget::kCategoryCount && std::size( kCategoryIcons ) == TerrainWidget::kCategoryCount );
    const char* kToolNames[] = { "Raise", "Lower", "Smooth", "Flatten", "Paint", "Erase" };
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
    actions.Register( { "View.TreesWorkspace", "Trees Workspace (MitchTree)", "View", 0, 0, [this]() { IsOpen = true; m_focusRequested = true; SetCategory( kTreesCategory ); } } );
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
    actions.Register( { "Terrain.Cook", "Cook Streamed Terrain...", "Terrain", 0, 0, [this]() { StreamCookDialog(); }, [this]() { return m_active && !IsPlaying() && !IsCooking(); } } );
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
            EndFoliageStroke();
            if( m_active ) m_active->EndStroke();
            if( id == "File.Save" )
            {
                if( m_category == 3 ) static_cast<EditorApp*>( GetEngine().GetGame() )->SaveScene( false );
                else m_active->Save();
            }
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
    EndFoliageStroke();
    FinishStreamCook( true );
    for( auto& document : m_documents ) document->EndStroke();
    if( AutomaticRecoveryEnabled() ) WriteRecoveries();
    m_active = nullptr; m_documents.clear();
    m_recoveryDirtyDocuments.clear();
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
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::GetColorU32( ImGuiCol_Text );
    EditorIcons::Draw( draw, icon, ImVec2( start.x + ( size.x - iconSize ) * 0.5f, iconY ), iconSize, color );
    if( !compact )
    {
        const float textWidth = ImGui::CalcTextSize( label ).x;
        draw->PushClipRect( start, ImVec2( start.x + size.x, start.y + size.y ), true );
        draw->AddText( ImVec2( start.x + std::max( 0.f, ( size.x - textWidth ) * 0.5f ), start.y + size.y - ImGui::GetTextLineHeight() - ImGui::GetStyle().FramePadding.y ), color, label );
        draw->PopClipRect();
    }
    ImGui::EndDisabled();
    if( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) ) EditorIcons::Tooltip( label, description );
    ImGui::PopID();
    return clicked;
}
void TerrainWidget::Planned( const char* icon, const char* label, const char* description )
{
    ImGui::TableNextColumn();
    Button( icon, label, ( std::string( "Planned: " ) + description ).c_str(), false );
}

void TerrainWidget::DrawTrees()
{
    if( m_treeListStale )
    {
        m_treeListStale = false;
        m_treeList.clear();
        std::error_code code;
        const std::filesystem::path root = Path( "Assets" ).FullPath;
        for( auto it = std::filesystem::recursive_directory_iterator( root, std::filesystem::directory_options::skip_permission_denied, code ); !code && it != std::filesystem::recursive_directory_iterator(); it.increment( code ) )
        {
            if( !it->is_regular_file( code ) || it->path().extension() != ".mtree" ) continue;
            MitchTree::TreeRecipe recipe;
            std::string error;
            const char* icon = MitchTree::LoadRecipe( it->path().string(), recipe, error ) ? TreeWidget::IconFor( recipe ) : "alert-triangle";
            m_treeList.emplace_back( Path( it->path().string() ).GetLocalPathString(), icon );
        }
        std::sort( m_treeList.begin(), m_treeList.end() );
    }
    ImGui::SeparatorText( "Project Trees" );
    if( m_treeList.empty() )
    {
        ImGui::TextWrapped( "No tree recipes yet. New Tree makes one from a preset (Oak, Pine, Palm...)." );
        return;
    }
    auto target = Selection::Get().GetActive();
    const bool canPlant = target && target->GetComponentByName( "TerrainFoliage" ) && !IsPlaying();
    if( !canPlant ) ImGui::TextDisabled( "Select a terrain with foliage to plant trees." );
    for( const auto& [path, icon] : m_treeList )
    {
        ImGui::PushID( path.c_str() );
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float line = ImGui::GetTextLineHeight();
        EditorIcons::Draw( ImGui::GetWindowDrawList(), icon, ImVec2( at.x, at.y + std::round( ( ImGui::GetFrameHeight() - line ) * 0.5f ) ), line, ImGui::GetColorU32( ImGuiCol_Text ) );
        ImGui::Dummy( ImVec2( line, ImGui::GetFrameHeight() ) );
        ImGui::SameLine( 0, ImGuiTheme::InnerGap() * 1.5f );
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( std::filesystem::path( path ).stem().string().c_str() );
        if( ImGui::IsItemHovered() ) ImGui::SetTooltip( "%s", path.c_str() );
        const float buttons = ImGuiTheme::IconButtonsWidth( 2 ) - ImGuiTheme::InnerGap();
        ImGui::SameLine( std::max( ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - buttons, ImGui::GetCursorPosX() ) );
        if( Button( "pencil", "Edit", "Open in MitchTree.", true, false, true ) )
        {
            OpenTreeEvent open;
            open.TreePath = path;
            open.Fire();
        }
        ImGui::SameLine( 0, ImGuiTheme::InnerGap() );
        if( Button( "plant", "Add to Foliage", "Add as a species of the selected terrain's foliage, then paint it in the Foliage workspace.", canPlant, false, true ) && !AddFoliageModel( path ) )
            m_error = "Couldn't add " + path + " to the foliage palette";
        ImGui::PopID();
    }
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
            for( int i = 0; i < 2; ++i )
            {
                ImGui::TableNextColumn();
                const auto tool = i ? TerrainData::BrushTool::Erase : TerrainData::BrushTool::Paint;
                if( Button( i ? "eraser" : "brush", i ? "Erase" : "Paint", "Drag in World View to blend the selected surface layer. Shift reverses paint and erase.", m_active && !IsPlaying(), BrushEnabled && Brush.Tool == tool ) )
                { m_active->EndStroke(); Brush.Tool = tool; BrushEnabled = true; }
            }
            Planned( "blur", "Blend", "Smooth transitions between material layers." );
            ImGui::TableNextColumn();
            if( Button( "bucket", "Fill", "Fill the whole terrain with the selected layer. Undo restores the previous weights.", m_active && !IsPlaying() ) ) FillLayer( Brush.Layer );
            Planned( "color-swatch", "Layers", "Create and reorder terrain material layers." );
            ImGui::TableNextColumn();
            if( Button( "eraser", "Clear", "Reset all weights to the first layer. Undo restores the previous surface.", m_active && !IsPlaying() ) ) FillLayer( 0 );
            Planned( "ruler", "Slope", "Paint using slope limits." );
            Planned( "mountain", "Altitude", "Paint within an elevation band." );
            Planned( "mask", "Masks", "Combine procedural painting masks." );
            ImGui::TableNextColumn();
            if( Button( "sphere", "Material", "Edit the selected layer's maps, shading and tiling.", m_active && !IsPlaying() ) ) EditSurfaceLayer();
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
            ImGui::TableNextColumn(); if( Button( "package", "Cook", "Cook a streamed runtime terrain with coarse coverage and independent detail blocks.", m_active && !IsPlaying() && !IsCooking() ) ) StreamCookDialog();
        }
        else if( m_category == 3 )
        {
            ImGui::TableNextColumn();
            if( Button( "tree", "Paint", "Paint the selected foliage model in World View.", m_active && !IsPlaying(), BrushEnabled && !FoliageBrush.Erase ) ) { EndFoliageStroke(); FoliageBrush.Erase = false; BrushEnabled = true; }
            ImGui::TableNextColumn();
            if( Button( "eraser", "Erase", "Erase selected foliage; hold Shift to reverse paint/erase.", m_active && !IsPlaying(), BrushEnabled && FoliageBrush.Erase ) ) { EndFoliageStroke(); FoliageBrush.Erase = true; BrushEnabled = true; }
            ImGui::TableNextColumn();
            if( Button( "magnet", "Reapply", "Fit foliage to the current source heights and normals. Undo restores placement.", m_active && !IsPlaying() ) ) ReprojectFoliage();
            ImGui::TableNextColumn();
            if( Button( "eye", "Visibility", "Toggle the selected terrain's foliage rendering.", !IsPlaying() ) )
            {
                auto entity = Selection::Get().GetActive();
                if( entity && entity->GetComponentByName( "TerrainFoliage" ) )
                {
                    const auto before = EditorOps::CaptureComponent( *entity.Get(), "TerrainFoliage" );
                    auto* foliage = entity->GetComponentByName( "TerrainFoliage" ); foliage->SetEnabled( !foliage->IsEnabled() );
                    EditorOps::RecordComponentEdit( *entity.Get(), "TerrainFoliage", before, EditorOps::CaptureComponent( *entity.Get(), "TerrainFoliage" ), "Foliage visibility" );
                }
            }
            Planned( "plant", "Grass", "Dense grass rendering and wind." );
            Planned( "mask", "Masks", "Paint using terrain layers and exclusion masks." );
            Planned( "refresh", "Reseed", "Regenerate procedural scattering rules." );
        }
        else if( m_category == kTreesCategory )
        {
            ImGui::TableNextColumn();
            if( Button( "plus", "New Tree", "Create a tree recipe from a preset and edit it in MitchTree.", !IsPlaying() ) ) ImGui::OpenPopup( "NewTreePreset" );
            if( ImGui::BeginPopup( "NewTreePreset" ) )
            {
                for( const std::string& preset : MitchTree::PresetNames() )
                {
                    if( !ImGui::Selectable( preset.c_str() ) ) continue;
                    // Assets/Trees/<Preset>.mtree, numbered when taken.
                    const std::filesystem::path folder = Path( "Assets/Trees" ).FullPath;
                    std::error_code code;
                    std::filesystem::create_directories( folder, code );
                    std::filesystem::path file = folder / ( preset + ".mtree" );
                    for( int n = 2; std::filesystem::exists( file ); ++n ) file = folder / ( preset + " " + std::to_string( n ) + ".mtree" );
                    MitchTree::TreeRecipe recipe;
                    std::string error;
                    if( MitchTree::MakePreset( preset, recipe ) && MitchTree::SaveRecipe( file.string(), recipe, error ) )
                    {
                        OpenTreeEvent open;
                        open.TreePath = Path( file.string() ).GetLocalPathString();
                        open.Fire();
                        m_treeListStale = true;
                    }
                    else m_error = "Couldn't create " + file.string() + ": " + error;
                }
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            if( Button( "refresh", "Refresh", "Find the project's tree recipes again." ) ) m_treeListStale = true;
            Planned( "trees", "Scatter", "Scatter trees over the terrain by altitude and slope, with a treeline." );
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
            DrawLodButtons();
            Planned( "border-all", "Seams", "Highlight shared tile edges." );
            Planned( "mountain", "Heights", "Visualize source elevations." );
            Planned( "color-swatch", "Weights", "Inspect material layer weights." );
            Planned( "sphere", "Normals", "Visualize heightfield normals." );
            Planned( "world", "Collision", "Compare static collision with the authoring surface." );
            Planned( "grid-dots", "Navmesh", "Inspect navigation rebuild status." );
            DrawResidencyButton();
            Planned( "search", "Validate", "Check source generations, borders and derived data." );
            ImGui::TableNextColumn();
            if( Button( "settings", "Budgets", "Open Project Settings > Terrain to tune shared CPU/GPU/collision limits and concurrent loads." ) )
                EditorActions::Get().Execute( "Edit.ProjectSettings" );
            Planned( "tools", "Repair", "Repair or compact terrain asset data." );
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled( "Dimmed tools are planned. Hover for details." );
    if( m_category == 5 ) { DrawStreamingControls(); DrawLodControls(); }
}

void TerrainWidget::DrawBrush()
{
    ImGui::SeparatorText( "Brush" );
    if( Button( BrushEnabled ? "hand-stop" : "pointer", BrushEnabled ? "Stop brush" : "Enable brush", "Enable or release terrain input in World View.", m_active && !IsPlaying(), BrushEnabled, true ) )
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
    ImGui::TextDisabled( m_category == 1 ? "Drag to paint  /  Shift reverses paint & erase" : "Drag to sculpt  /  Shift reverses raise & lower" );
    ImGui::TextDisabled( "Esc cancels stroke  /  RMB navigates" );
}

void TerrainWidget::Render()
{
    if( m_foliageStroke.Active() )
    {
        if( ImGui::GetIO().AppFocusLost || ImGui::IsKeyPressed( ImGuiKey_Escape ) ) EndFoliageStroke( true );
        else if( !IsOpen || IsPlaying() || !BrushEnabled || !ImGui::IsMouseDown( ImGuiMouseButton_Left ) ) EndFoliageStroke();
    }
    for( auto& document : m_documents ) if( document->IsPreparingPreview() ) document->TickPreview();
    FinishStreamCook();
    DrawStreamCook();
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
    if( Button( "device-floppy", m_category == 3 ? "Save Scene" : "Save Terrain", m_category == 3 ? "Save foliage with the scene." : "Save this terrain asset. Scene references are saved separately.", editing && ( m_category == 3 ? UndoStack::Get().IsDirty() : m_active && m_active->IsDirty() ), false, true ) )
    { EndFoliageStroke(); if( m_category == 3 ) static_cast<EditorApp*>( GetEngine().GetGame() )->SaveScene( false ); else m_active->Save(); }
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
    if( Button( "arrow-back-up", "Undo Terrain", "Undo the latest terrain stroke or brush setting.", editing && GetHistory().CanUndo(), false, true ) ) { EndFoliageStroke(); if( m_active ) m_active->EndStroke(); GetHistory().Undo(); }
    ImGui::SameLine( 0, ImGuiTheme::InnerGap() );
    if( Button( "arrow-forward-up", "Redo Terrain", "Redo the latest terrain stroke or brush setting.", editing && GetHistory().CanRedo(), false, true ) ) { EndFoliageStroke(); if( m_active ) m_active->EndStroke(); GetHistory().Redo(); }
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
        if( m_active->IsPreparingPreview() ) ImGui::TextDisabled( "Preparing latest terrain preview..." );
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
    // Two rows whatever the count, so the tools below keep their place.
    if( ImGui::BeginTable( "Modes", ( kCategoryCount + 1 ) / 2, ImGuiTableFlags_SizingStretchSame ) )
    {
        for( int i = 0; i < kCategoryCount; ++i )
        {
            ImGui::TableNextColumn();
            const char* tooltip = i == 0 ? "Sculpt source heights in World View." : i == kTreesCategory ? "Generated trees (MitchTree): edit them and plant them as foliage." : "Browse the planned tools in this workspace.";
            if( Button( kCategoryIcons[i], kCategories[i], tooltip, true, m_category == i ) ) SetCategory( i );
        }
        ImGui::EndTable();
    }
    if( m_active && !m_active->IsPreparingPreview() && !m_active->GetPreviewError().empty() && ImGui::SmallButton( "Retry preview" ) ) m_active->RetryPreview();
    DrawTools();
    if( m_category == 0 ) DrawBrush();
    if( m_category == 1 ) DrawPaint();
    if( m_category == 3 ) DrawFoliage();
    if( m_category == kTreesCategory ) DrawTrees();
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
            ImGui::TableNextColumn(); if( Button( "package", "Build", "Cook the current heights for runtime streaming.", m_active && editing && !IsCooking() ) ) StreamCookDialog();
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
    if( !IsOpen || !BrushEnabled || ( m_category != 0 && m_category != 1 && m_category != 3 ) || !m_active || IsPlaying() ) return false;
    auto entity = Selection::Get().GetActive();
    if( !entity || !entity->IsActiveInHierarchy() ) return false;
    const auto* terrain = entity->TryGetComponent<Terrain>();
    auto* transform = entity->TryGetComponent<Transform>();
    return terrain && transform && terrain->GetData() && Key( terrain->Asset.GetLocalPathString() ) == Key( m_active->GetPath() ) && TerrainData::IsTranslationOnly( transform->GetLocalToWorldMatrix() );
}

bool TerrainWidget::DrawViewport( SceneViewWidget& view, const Vector2& screenMin, const Vector2& screenSize )
{
    if( m_category == 3 ) return DrawFoliageViewport( view, screenMin, screenSize );
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
        if( m_category == 1 && io.KeyShift ) brush.Tool = brush.Tool == TerrainData::BrushTool::Erase ? TerrainData::BrushTool::Paint : TerrainData::BrushTool::Erase;
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
