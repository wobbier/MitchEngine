#include "PCH.h"
#include "NavMeshModifiers.h"
#include "Engine/ProjectSettings.h"

static_assert( ProjectSettings::kNavAreaCount == NavAreas::kCount, "navigation area tables must match" );

ME_REFLECT_BEGIN( NavMeshModifier )
    ME_FIELD( IgnoreFromBuild ).Tooltip( "Leave this geometry out of the bake" );
    ME_FIELD( OverrideArea );
    ME_FIELD( Area ).Choices( NavAreas::kCount, NavigationUI::AreaName ).Tooltip( "Areas and their costs are in Project Settings > Navigation" );
    ME_FIELD( ApplyToChildren );
ME_REFLECT_END()

ME_REFLECT_BEGIN( NavMeshModifierVolume )
    ME_FIELD( Center );
    ME_FIELD( Size );
    ME_FIELD( Area ).Choices( NavAreas::kCount, NavigationUI::AreaName );
ME_REFLECT_END()

ME_REFLECT_ENUM( NavObstacleShape, { { "Box", NavObstacleShape::Box }, { "Cylinder", NavObstacleShape::Cylinder } } )

ME_REFLECT_BEGIN( NavMeshObstacle )
    ME_FIELD( Shape );
    ME_FIELD( Center );
    ME_FIELD( Size ).Tooltip( "Box: size; cylinder: diameter (x) and height (y). Local to this entity" );
    ME_FIELD( MoveThreshold ).Range( 0.f, 5.f ).Tooltip( "Distance it must move before the navmesh is re-carved" );
    ME_FIELD( CarveDelay ).Range( 0.f, 5.f ).Tooltip( "Seconds it must stand still before carving (moving obstacles don't churn the navmesh)" );
ME_REFLECT_END()

ME_REFLECT_BEGIN( NavMeshLink )
    ME_FIELD( StartPoint ).Tooltip( "Local to this entity" );
    ME_FIELD( EndPoint ).Tooltip( "Local to this entity" );
    ME_FIELD( Radius ).Range( 0.05f, 10.f ).Tooltip( "How close to each end the navmesh must be for the link to connect" );
    ME_FIELD( Bidirectional );
    ME_FIELD( Area ).Choices( NavAreas::kCount, NavigationUI::AreaName );
ME_REFLECT_END()


namespace NavigationUI
{
    std::string AreaName( int InArea )
    {
        return ProjectSettings::Get().GetNavAreaLabel( InArea );
    }


    std::string LayerName( int InLayer )
    {
        return ProjectSettings::Get().GetLayerLabel( InLayer );
    }
}


NavMeshModifier::NavMeshModifier()
    : Component( "NavMeshModifier" )
{
}


void NavMeshModifier::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
}


NavMeshModifierVolume::NavMeshModifierVolume()
    : Component( "NavMeshModifierVolume" )
{
}


void NavMeshModifierVolume::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
}


NavMeshObstacle::NavMeshObstacle()
    : Component( "NavMeshObstacle" )
{
}


void NavMeshObstacle::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
}


NavMeshLink::NavMeshLink()
    : Component( "NavMeshLink" )
{
}


void NavMeshLink::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
}
