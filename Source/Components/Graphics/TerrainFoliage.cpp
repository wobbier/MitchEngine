#include "PCH.h"
#include "TerrainFoliage.h"
#if USING( ME_EDITOR )
#include <imgui.h>
#endif

ME_REFLECT_BEGIN( TerrainFoliage )
    ME_FIELD( DrawDistance ).Range( 1.f, 10000.f );
    ME_FIELD( CastShadows );
ME_REFLECT_END()


bool TerrainFoliage::SetData( Foliage::Data data )
{
    if( !data.Validate( m_error ) ) return false;
    m_data = std::move( data ); ++m_revision; m_error.clear(); return true;
}


void TerrainFoliage::OnSerialize( json& out )
{
    out["Foliage"] = Foliage::Encode( m_data ); out["DrawDistance"] = DrawDistance; out["CastShadows"] = CastShadows;
}


void TerrainFoliage::OnDeserialize( const json& in )
{
    try
    {
        Foliage::Data data;
        if( in.contains( "Foliage" ) && !Foliage::Decode( in["Foliage"], data, m_error ) ) return;
        const float distance = in.value( "DrawDistance", 250.f );
        if( !std::isfinite( distance ) || distance < 1 || distance > 10000 ) { m_error = "Invalid foliage draw distance"; return; }
        const bool shadows = in.value( "CastShadows", true );
        if( SetData( std::move( data ) ) ) { DrawDistance = distance; CastShadows = shadows; SetEnabled( in.value( "Enabled", true ) ); }
    }
    catch( const std::exception& e ) { m_error = e.what(); }
}


#if USING( ME_EDITOR )
void TerrainFoliage::OnEditorInspect()
{
    ImGui::Text( "%zu instances / %zu species / %zu mesh parts", m_data.Instances.size(), m_data.Palette.size(), m_drawCount );
    ImGui::TextWrapped( "Edit in Terrain > Foliage. Saved with the scene. Visual instances have no collision." );
    if( !GetError().empty() ) ImGui::TextWrapped( "%s", GetError().c_str() );
}
#endif
