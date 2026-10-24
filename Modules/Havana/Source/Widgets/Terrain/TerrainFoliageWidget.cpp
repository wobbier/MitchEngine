#include "TerrainWidget.h"
#if USING( ME_EDITOR )
#include "Components/Graphics/TerrainFoliage.h"
#include "Components/Graphics/Terrain.h"
#include "Components/Transform.h"
#include "Editor/EditorOperations.h"
#include "Editor/Selection.h"
#include "Editor/SceneTools.h"
#include "Engine/World.h"
#include "Widgets/SceneViewWidget.h"
#include "Widgets/Tree/TreeWidget.h"
#include "Utils/EditorIcons.h"
#include "Events/EditorEvents.h"
#include "Types/AssetDescriptor.h"
#include "ImGuiTheme.h"
#include "misc/cpp/imgui_stdlib.h"
#include <algorithm>
#include <cmath>

namespace
{
    EntityHandle Target()
    {
        auto entity = Selection::Get().GetActive(); return entity && entity->HasComponent<Terrain>() && entity->HasComponent<Transform>() ? entity : EntityHandle();
    }


    class FoliageBrushCommand final : public UndoCommand
    {
    public:
        FoliageBrushCommand( Foliage::Brush before, Foliage::Brush after ) : Before( before ), After( after ) {}
        void Undo() override { if( auto* widget = TerrainWidget::Get() ) widget->SetFoliageBrush( Before, false ); }
        void Redo() override { if( auto* widget = TerrainWidget::Get() ) widget->SetFoliageBrush( After, false ); }
        std::string GetName() const override { return "Foliage brush settings"; }
        bool AffectsSavedState() const override { return false; }
    private:
        Foliage::Brush Before, After;
    };


    class PaletteCommand final : public UndoCommand
    {
    public:
        PaletteCommand( uint64_t id, std::vector<Foliage::Species> before, std::vector<Foliage::Species> after ) : ID( id ), Before( std::move( before ) ), After( std::move( after ) ) {}
        void Undo() override { Apply( Before ); }
        void Redo() override { Apply( After ); }
        std::string GetName() const override { return "Foliage palette"; }
    private:
        void Apply( const std::vector<Foliage::Species>& palette )
        {
            auto entity = EditorOps::GetWorld().FindEntityByGUID( ID ); auto* foliage = entity ? entity->TryGetComponent<TerrainFoliage>() : nullptr;
            if( foliage ) { auto data = foliage->GetData(); data.Palette = palette; foliage->SetData( std::move( data ) ); }
        }
        uint64_t ID; std::vector<Foliage::Species> Before, After;
    };


    class FoliageCommand final : public UndoCommand
    {
    public:
        FoliageCommand( uint64_t id, std::vector<Foliage::Instance> added, std::vector<Foliage::Instance> removed, uint64_t before, uint64_t after )
            : ID( id ), Added( std::move( added ) ), Removed( std::move( removed ) ), Before( before ), After( after ) {}
        void Undo() override { Apply( Added, Removed, Before ); }
        void Redo() override { Apply( Removed, Added, After ); }
        std::string GetName() const override { return "Paint / erase foliage"; }
    private:
        void Apply( const std::vector<Foliage::Instance>& remove, const std::vector<Foliage::Instance>& add, uint64_t next )
        {
            auto entity = EditorOps::GetWorld().FindEntityByGUID( ID ); auto* foliage = entity ? entity->TryGetComponent<TerrainFoliage>() : nullptr;
            if( !foliage ) return;
            auto data = foliage->GetData(); std::set<uint64_t> ids; for( const auto& i : remove ) ids.insert( i.ID );
            std::erase_if( data.Instances, [&]( const auto& i ) { return ids.contains( i.ID ); } ); data.Instances.insert( data.Instances.end(), add.begin(), add.end() );
            std::sort( data.Instances.begin(), data.Instances.end(), []( const auto& a, const auto& b ) { return a.ID < b.ID; } ); data.NextID = next; foliage->SetData( std::move( data ) );
        }
        uint64_t ID; std::vector<Foliage::Instance> Added, Removed; uint64_t Before, After;
    };
}


void TerrainWidget::SetFoliageBrush( const Foliage::Brush& brush, bool record )
{
    if( IsPlaying() || m_foliageStroke.Active() ) return;
    for( float value : { brush.Radius, brush.Density, brush.MinSpacing, brush.MinScale, brush.MaxScale, brush.MaxSlope, brush.HeightOffset } ) if( !std::isfinite( value ) ) return;
    const auto before = FoliageBrush; FoliageBrush = brush;
    if( record && before != brush ) UndoStack::Get().Push( std::make_unique<FoliageBrushCommand>( before, brush ) );
}


bool TerrainWidget::AddFoliageModel( const std::string& path )
{
    EndFoliageStroke(); auto entity = Target();
    if( !entity || IsPlaying() || path.empty() ) { m_error = "Select the terrain instance before adding a foliage model"; return false; }
    UndoTransaction transaction( "Add foliage model" );
    if( !entity->HasComponent<TerrainFoliage>() ) EditorOps::AddComponent( *entity.Get(), "TerrainFoliage" );
    auto& foliage = entity->GetComponent<TerrainFoliage>(); auto data = foliage.GetData(); const auto before = data.Palette;
    if( data.Palette.size() >= Foliage::MaxSpecies ) { m_error = "The foliage palette is full (16 species)"; return false; }
    data.Palette.push_back( { std::filesystem::path( path ).stem().string(), Path( path ).GetLocalPathString() } );
    if( !foliage.SetData( data ) ) { m_error = foliage.GetError(); return false; }
    FoliageBrush.SpeciesIndex = unsigned( data.Palette.size() - 1 );
    UndoStack::Get().Push( std::make_unique<PaletteCommand>( entity->GetGUID(), before, data.Palette ) ); m_error.clear(); return true;
}


bool TerrainWidget::AddFoliageStarters()
{
    UndoTransaction transaction( "Add starter foliage" );
    for( const char* path : { "Engine/Assets/Models/Foliage/Tree.obj", "Engine/Assets/Models/Foliage/Bush.obj", "Engine/Assets/Models/Foliage/Rock.obj" } ) if( !AddFoliageModel( path ) ) return false;
    FoliageBrush.SpeciesIndex = 0; return true;
}


bool TerrainWidget::BeginFoliageStroke( double x, double z, bool reverse )
{
    EndFoliageStroke(); auto entity = Target();
    auto* foliage = entity ? entity->TryGetComponent<TerrainFoliage>() : nullptr;
    if( IsPlaying() || !m_active || !foliage || !foliage->IsEnabled() || std::filesystem::path( entity->GetComponent<Terrain>().Asset.FullPath ).lexically_normal() != std::filesystem::path( Path( m_active->GetPath() ).FullPath ).lexically_normal() || !TerrainData::IsTranslationOnly( entity->GetComponent<Transform>().GetLocalToWorldMatrix() ) ) return false;
    auto brush = FoliageBrush; if( reverse ) brush.Erase = !brush.Erase;
    m_foliageBefore = foliage->GetData(); m_foliageEntity = entity->GetGUID();
    if( !m_foliageStroke.Begin( m_active->GetField(), m_foliageBefore, brush, x, z, m_error ) ) return false;
    return foliage->SetData( m_foliageStroke.GetData() );
}


bool TerrainWidget::MoveFoliageStroke( double x, double z )
{
    auto entity = EditorOps::GetWorld().FindEntityByGUID( m_foliageEntity ); auto* foliage = entity ? entity->TryGetComponent<TerrainFoliage>() : nullptr;
    if( !m_active || !foliage || !m_foliageStroke.Move( m_active->GetField(), x, z, m_error ) ) { EndFoliageStroke( true ); return false; }
    return foliage->SetData( m_foliageStroke.GetData() );
}


void TerrainWidget::EndFoliageStroke( bool cancel )
{
    if( !m_foliageStroke.Active() ) return;
    auto entity = EditorOps::GetWorld().FindEntityByGUID( m_foliageEntity ); auto* foliage = entity ? entity->TryGetComponent<TerrainFoliage>() : nullptr;
    if( foliage )
    {
        if( cancel ) foliage->SetData( m_foliageBefore );
        else if( !m_foliageStroke.Added().empty() || !m_foliageStroke.Removed().empty() )
            UndoStack::Get().Push( std::make_unique<FoliageCommand>( m_foliageEntity, m_foliageStroke.Added(), m_foliageStroke.Removed(), m_foliageBefore.NextID, foliage->GetData().NextID ) );
    }
    m_foliageStroke.Stop(); m_foliageBefore = {}; m_foliageEntity = 0;
}


bool TerrainWidget::ClearFoliage( bool all )
{
    EndFoliageStroke(); auto entity = Target(); auto* foliage = entity ? entity->TryGetComponent<TerrainFoliage>() : nullptr;
    if( !foliage || IsPlaying() ) return false;
    auto data = foliage->GetData(); std::vector<Foliage::Instance> removed;
    std::erase_if( data.Instances, [&]( const auto& i ) { if( all || i.SpeciesIndex == FoliageBrush.SpeciesIndex ) { removed.push_back( i ); return true; } return false; } );
    if( removed.empty() ) return true;
    if( !foliage->SetData( data ) ) return false;
    UndoStack::Get().Push( std::make_unique<FoliageCommand>( entity->GetGUID(), std::vector<Foliage::Instance>{}, removed, data.NextID, data.NextID ) ); return true;
}


bool TerrainWidget::ReprojectFoliage()
{
    EndFoliageStroke(); auto entity = Target(); auto* foliage = entity ? entity->TryGetComponent<TerrainFoliage>() : nullptr;
    if( !m_active || !foliage || IsPlaying() || std::filesystem::path( entity->GetComponent<Terrain>().Asset.FullPath ).lexically_normal() != std::filesystem::path( Path( m_active->GetPath() ).FullPath ).lexically_normal() ) return false;
    auto data = foliage->GetData(); std::vector<Foliage::Instance> before, after;
    for( auto& i : data.Instances )
    {
        const auto hit = m_active->GetField().Query( i.Position.x, i.Position.z ); if( hit.Status != TerrainData::QueryStatus::Ready ) { m_error = "Canonical source heights must be ready to reapply foliage"; return false; }
        before.push_back( i ); i.Position.y = hit.Height + FoliageBrush.HeightOffset; i.Normal = hit.Normal; after.push_back( i );
    }
    if( !foliage->SetData( data ) ) return false;
    if( !before.empty() ) UndoStack::Get().Push( std::make_unique<FoliageCommand>( entity->GetGUID(), after, before, data.NextID, data.NextID ) ); return true;
}


void TerrainWidget::DrawFoliage()
{
    ImGui::SeparatorText( "Foliage palette" );
    auto entity = Target(); auto* foliage = entity ? entity->TryGetComponent<TerrainFoliage>() : nullptr;
    if( !entity ) { ImGui::TextWrapped( "Select a terrain instance in the scene." ); return; }
    ImGui::TextWrapped( "Foliage is saved with the scene (Ctrl+S). Visual instances have no collision." );
    ImGui::BeginDisabled( IsPlaying() || m_foliageStroke.Active() );
    if( ImGui::Button( "Add model..." ) )
    {
        const auto guid = entity->GetGUID();
        RequestAssetSelectionEvent event( [this, guid]( Path path ) { auto selected = Target(); if( selected && selected->GetGUID() == guid ) AddFoliageModel( path.GetLocalPathString() ); }, AssetType::Model ); event.Fire();
    }
    if( ImGui::BeginDragDropTarget() )
    {
        if( ImGui::AcceptDragDropPayload( AssetDescriptor::kDragAndDropPayload ) ) if( const auto* asset = AssetDescriptor::GetDragged(); asset && asset->Type == AssetType::Model ) AddFoliageModel( asset->FullPath.GetLocalPathString() );
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine(); if( ImGui::Button( "Starter meshes" ) ) { AddFoliageStarters(); foliage = entity->TryGetComponent<TerrainFoliage>(); }
    if( foliage )
    {
        const auto& data = foliage->GetData();
        for( unsigned s = 0; s < data.Palette.size(); ++s )
        {
            ImGui::PushID( static_cast<int>( s ) );
            if( ImGui::Selectable( data.Palette[s].Name.c_str(), FoliageBrush.SpeciesIndex == s, ImGuiSelectableFlags_AllowDoubleClick ) ) FoliageBrush.SpeciesIndex = s;
            // MitchTree species open in the tree editor (double-click or the context menu).
            const bool isTree = TreeWidget::IsTreePath( data.Palette[s].Model );
            if( isTree ) ImGui::SetItemTooltip( "%s\nDouble-click to edit in MitchTree.", data.Palette[s].Model.c_str() );
            bool edit = isTree && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left );
            if( isTree && ImGui::BeginPopupContextItem( "Species" ) )
            {
                edit = EditorIcons::MenuItem( "pencil", "Edit in MitchTree" ) || edit;
                ImGui::EndPopup();
            }
            if( edit ) { OpenTreeEvent open; open.TreePath = data.Palette[s].Model; open.Fire(); }
            ImGui::PopID();
        }
        ImGui::Text( "%zu / %zu instances", data.Instances.size(), Foliage::MaxInstances );
        if( ImGui::Button( "Clear selected" ) ) ClearFoliage( false ); ImGui::SameLine(); if( ImGui::Button( "Clear all" ) ) ClearFoliage( true );
        if( !foliage->GetError().empty() ) ImGui::TextWrapped( "%s", foliage->GetError().c_str() );
    }
    ImGui::SeparatorText( "Foliage brush" );
    ImGui::Checkbox( "Brush enabled", &BrushEnabled );
    const auto record = [&]( const Foliage::Brush& before ) { const auto after = FoliageBrush; FoliageBrush = before; SetFoliageBrush( after ); };
    const auto control = [&]( const auto& draw ) {
        const auto before = FoliageBrush; draw();
        if( ImGui::IsItemActivated() ) m_foliageControlBefore = before;
        if( ImGui::IsItemDeactivatedAfterEdit() ) record( m_foliageControlBefore );
    };
    control( [&]() { ImGui::DragFloat( "Radius", &FoliageBrush.Radius, 0.1f, 0.25f, 128, "%.2f m", ImGuiSliderFlags_AlwaysClamp ); } );
    control( [&]() { ImGui::DragFloat( "Density", &FoliageBrush.Density, 0.01f, 0.005f, 10, "%.3f / m2", ImGuiSliderFlags_AlwaysClamp ); } );
    control( [&]() { ImGui::DragFloat( "Min spacing", &FoliageBrush.MinSpacing, 0.1f, 0.25f, 100, "%.2f m", ImGuiSliderFlags_AlwaysClamp ); } );
    control( [&]() { ImGui::DragFloatRange2( "Scale", &FoliageBrush.MinScale, &FoliageBrush.MaxScale, 0.01f, 0.01f, 100 ); } );
    control( [&]() { ImGui::SliderFloat( "Max slope", &FoliageBrush.MaxSlope, 0, 90, "%.0f degrees" ); } );
    control( [&]() { ImGui::DragFloat( "Height offset", &FoliageBrush.HeightOffset, 0.01f, -10, 10, "%.2f m", ImGuiSliderFlags_AlwaysClamp ); } );
    auto before = FoliageBrush; if( ImGui::Checkbox( "Align to normal", &FoliageBrush.AlignNormal ) ) record( before );
    before = FoliageBrush; if( ImGui::Checkbox( "Erase all species", &FoliageBrush.EraseAll ) ) record( before );
    control( [&]() { int seed = int( FoliageBrush.Seed ); if( ImGui::InputInt( "Seed", &seed ) ) FoliageBrush.Seed = uint32_t( seed ); } );
    ImGui::EndDisabled();
    ImGui::TextDisabled( "Drag to paint  /  Shift reverses paint & erase" ); ImGui::TextDisabled( "Esc cancels stroke  /  RMB navigates" );
}


bool TerrainWidget::DrawFoliageViewport( SceneViewWidget& view, const Vector2& screenMin, const Vector2& screenSize )
{
    m_viewportOwnsActions = false; auto entity = Target(); auto& io = ImGui::GetIO();
    if( !WantsViewport() || !view.MainCamera ) { EndFoliageStroke(); return false; }
    m_viewportOwnsActions = view.IsFocused;
    if( m_foliageStroke.Active() && ( entity->GetGUID() != m_foliageEntity || io.AppFocusLost || ImGui::IsKeyPressed( ImGuiKey_Escape ) ) ) { EndFoliageStroke( true ); return true; }
    if( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) || !view.IsFocused ) EndFoliageStroke();
    if( io.KeyAlt || ImGui::IsMouseDown( ImGuiMouseButton_Right ) || ImGui::IsMouseDown( ImGuiMouseButton_Middle ) || view.IsUsingGuizmo ) { EndFoliageStroke(); return true; }
    if( !view.IsViewportHovered ) return true;
    const auto translation = entity->GetComponent<Transform>().GetWorldPosition();
    const Ray ray = SceneTools::ScreenRay( view.ViewportMousePosition, view.SceneViewRenderSize, view.MainCamera->View, view.MainCamera->ProjectionMatrix ); Vector3 point;
    if( !TerrainData::RaycastHeightfield( m_active->GetField(), Ray( ray.Origin - translation, ray.Direction ), point ) ) return true;
    auto* list = ImGui::GetWindowDrawList(); list->PushClipRect( { screenMin.x, screenMin.y }, { screenMin.x + screenSize.x, screenMin.y + screenSize.y }, true );
    const Matrix4 vp( view.MainCamera->ProjectionMatrix.GetInternalMatrix() * view.MainCamera->View.GetInternalMatrix() ); Vector2 previous; bool hasPrevious = false;
    for( int n = 0; n <= 96; ++n )
    {
        const float angle = n * 6.2831853f / 96; Vector3 p( point.x + FoliageBrush.Radius * std::cos( angle ), 0, point.z + FoliageBrush.Radius * std::sin( angle ) );
        const auto hit = m_active->GetField().Query( p.x, p.z ); Vector2 pixel;
        if( hit.Status != TerrainData::QueryStatus::Ready ) { hasPrevious = false; continue; } p.y = hit.Height + 0.04f;
        if( !SceneTools::WorldToScreen( p + translation, view.SceneViewRenderSize, vp, pixel ) ) { hasPrevious = false; continue; }
        pixel = screenMin + Vector2( pixel.x * screenSize.x / view.SceneViewRenderSize.x, pixel.y * screenSize.y / view.SceneViewRenderSize.y );
        if( hasPrevious ) list->AddLine( { previous.x, previous.y }, { pixel.x, pixel.y }, FoliageBrush.Erase != io.KeyShift ? IM_COL32( 255, 110, 90, 255 ) : IM_COL32( 100, 255, 120, 255 ), 2 );
        previous = pixel; hasPrevious = true;
    }
    list->PopClipRect();
    if( ImGui::IsMouseClicked( ImGuiMouseButton_Left ) ) { ImGui::SetWindowFocus(); BeginFoliageStroke( point.x, point.z, io.KeyShift ); }
    else if( m_foliageStroke.Active() ) MoveFoliageStroke( point.x, point.z );
    return true;
}
#endif
