#include "PCH.h"
#include "NavMeshSurface.h"
#include "Components/Navigation/NavMeshModifiers.h"
#include "Cores/NavigationCore.h"
#include "Engine/Engine.h"
#include "Engine/ProjectSettings.h"

#if USING( ME_EDITOR )
#include <imgui.h>
#endif

ME_REFLECT_ENUM( NavCollectObjects, { { "All", NavCollectObjects::All }, { "Children", NavCollectObjects::Children }, { "Volume", NavCollectObjects::Volume } } )
ME_REFLECT_ENUM( NavCollectGeometry, { { "Physics Colliders", NavCollectGeometry::PhysicsColliders }, { "Render Meshes", NavCollectGeometry::RenderMeshes }, { "Both", NavCollectGeometry::Both } } )
ME_REFLECT_ENUM( NavPartition, { { "Watershed", NavPartition::Watershed }, { "Monotone", NavPartition::Monotone }, { "Layers", NavPartition::Layers } } )

ME_REFLECT_BEGIN( NavMeshSurface )
    ME_FIELD( AgentRadius ).Category( "Agent" ).Range( 0.05f, 10.f );
    ME_FIELD( AgentHeight ).Category( "Agent" ).Range( 0.1f, 20.f );
    ME_FIELD( AgentMaxClimb ).Category( "Agent" ).Range( 0.f, 5.f ).Tooltip( "Step height the agent can walk up" );
    ME_FIELD( AgentMaxSlope ).Category( "Agent" ).Range( 0.f, 89.f ).Tooltip( "Degrees; steeper surfaces aren't walkable" );
    ME_FIELD( Collect ).Category( "Sources" ).Tooltip( "Which entities to bake" );
    ME_FIELD( UseGeometry ).Category( "Sources" );
    ME_FIELD( IncludeLayers ).Category( "Sources" ).MaskChoices( ProjectSettings::kLayerCount, NavigationUI::LayerName );
    ME_FIELD( DefaultArea ).Category( "Sources" ).Choices( NavAreas::kCount, NavigationUI::AreaName );
    ME_FIELD( VolumeCenter ).Category( "Sources" ).Tooltip( "Collect = Volume: the box to bake, local to this entity" );
    ME_FIELD( VolumeSize ).Category( "Sources" );
    ME_FIELD( CellSize ).Category( "Advanced" ).Range( 0.f, 2.f ).Tooltip( "Voxel size; 0 = a third of the agent radius" );
    ME_FIELD( CellHeight ).Category( "Advanced" ).Range( 0.f, 2.f ).Tooltip( "0 = half the cell size" );
    ME_FIELD( TileSize ).Category( "Advanced" ).Range( 16.f, 512.f ).Tooltip( "Cells per tile side; tiles bake in parallel" );
    ME_FIELD( MinRegionSize ).Category( "Advanced" ).Range( 0.f, 150.f ).Tooltip( "Cells; smaller islands are dropped" );
    ME_FIELD( MergeRegionSize ).Category( "Advanced" ).Range( 0.f, 150.f );
    ME_FIELD( Partition ).Category( "Advanced" );
    ME_FIELD( EdgeMaxLength ).Category( "Advanced" ).Range( 0.f, 50.f );
    ME_FIELD( EdgeMaxError ).Category( "Advanced" ).Range( 0.1f, 3.f );
    ME_FIELD( DetailSampleDistance ).Category( "Advanced" ).Range( 0.f, 16.f );
    ME_FIELD( DetailSampleMaxError ).Category( "Advanced" ).Range( 0.f, 16.f );
    ME_FIELD( NavMeshData ).Category( "Output" ).Tooltip( "Baked file; empty = <Scene>.<Entity>.navmesh next to the scene" );
    ME_FIELD( BakeOnLoad ).Category( "Output" ).Tooltip( "Bake when the scene loads (procedural levels)" );
ME_REFLECT_END()


NavMeshSurface::NavMeshSurface()
    : Component( "NavMeshSurface" )
{
}


void NavMeshSurface::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
}


NavBuildSettings NavMeshSurface::GetBuildSettings() const
{
    NavBuildSettings settings;
    settings.AgentRadius = AgentRadius;
    settings.AgentHeight = AgentHeight;
    settings.AgentMaxClimb = AgentMaxClimb;
    settings.AgentMaxSlope = AgentMaxSlope;
    settings.CellSize = CellSize;
    settings.CellHeight = CellHeight;
    settings.TileSize = TileSize;
    settings.MinRegionSize = MinRegionSize;
    settings.MergeRegionSize = MergeRegionSize;
    settings.Partition = Partition;
    settings.EdgeMaxLength = EdgeMaxLength;
    settings.EdgeMaxError = EdgeMaxError;
    settings.DetailSampleDistance = DetailSampleDistance;
    settings.DetailSampleMaxError = DetailSampleMaxError;
    return settings;
}


bool NavMeshSurface::HasNavMesh() const
{
    NavigationCore* core = GetEngine().Navigation;
    return core && core->GetNavMesh( *this ) != nullptr;
}


bool NavMeshSurface::IsBaking() const
{
    NavigationCore* core = GetEngine().Navigation;
    return core && core->IsBaking( *this );
}


float NavMeshSurface::GetBakeProgress() const
{
    NavigationCore* core = GetEngine().Navigation;
    return core ? core->GetBakeProgress( *this ) : 0.f;
}


void NavMeshSurface::Bake()
{
    if( NavigationCore* core = GetEngine().Navigation )
    {
        core->Bake( *this, true );
    }
}


void NavMeshSurface::Clear()
{
    if( NavigationCore* core = GetEngine().Navigation )
    {
        core->ClearNavMesh( *this );
    }
}


#if USING( ME_EDITOR )
void NavMeshSurface::OnEditorInspect()
{
    NavigationCore* core = GetEngine().Navigation;
    if( !core )
    {
        return;
    }
    core->MarkInspected( *this );
    const NavigationCore::SurfaceInfo info = core->GetSurfaceInfo( *this );
    ImGui::Separator();
    if( info.Baking )
    {
        ImGui::ProgressBar( info.Progress, ImVec2( -1.f, 0.f ), "Baking..." );
    }
    else
    {
        if( ImGui::Button( "Bake" ) )
        {
            Bake();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled( !info.Loaded );
        if( ImGui::Button( "Clear" ) )
        {
            Clear();
        }
        ImGui::EndDisabled();
    }
    if( info.Loaded )
    {
        ImGui::TextDisabled( "%d tile(s), %d polygon(s)", info.Tiles, info.Polygons );
        if( info.BakeMilliseconds > 0.f )
        {
            ImGui::TextDisabled( "Last bake: %.1f ms", info.BakeMilliseconds );
        }
        if( info.Agents > 0 )
        {
            ImGui::TextDisabled( "%d agent(s)", info.Agents );
        }
    }
    else if( !info.Baking )
    {
        ImGui::TextColored( ImVec4( 1.f, 0.75f, 0.3f, 1.f ), "Not baked" );
    }
    if( !info.LastError.empty() )
    {
        ImGui::TextColored( ImVec4( 1.f, 0.4f, 0.4f, 1.f ), "%s", info.LastError.c_str() );
    }
    ImGui::TextDisabled( "%s", info.DataPath.empty() ? "Save the scene to bake to a file" : info.DataPath.c_str() );
}
#endif
