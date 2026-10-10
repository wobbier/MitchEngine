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


NavMeshLink::NavMeshLink()
    : Component( "NavMeshLink" )
{
}


void NavMeshLink::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
}
