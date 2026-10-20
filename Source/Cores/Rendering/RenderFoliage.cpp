#include "PCH.h"
#include "RenderCore.h"
#include "Components/Graphics/TerrainFoliage.h"
#include "Components/Graphics/Mesh.h"
#include "Components/Transform.h"
#include "Engine/Engine.h"
#include "Resource/ResourceCache.h"
#include "Core/CommandLine.h"
#include "Utils/BGFXUtils.h"
#include "Renderer.h"
#include <algorithm>
#include <cmath>
#include <set>

void RenderCore::RemoveFoliageDraw( uint64_t id )
{
    auto found = m_foliageDraws.find( id ); if( found == m_foliageDraws.end() ) return;
    if( Moonlight::IsGpuAlive() ) for( const auto& [key, instance] : found->second.Instances ) for( const auto& mesh : instance.Parts )
        GetEngine().GetRenderer().GetMeshCache().Pop( mesh->GetId() );
    m_foliageDraws.erase( found );
}


void RenderCore::ClearFoliageDraws()
{
    while( !m_foliageDraws.empty() ) RemoveFoliageDraw( m_foliageDraws.begin()->first );
}


void RenderCore::PrepareFoliageDraw( Entity& entity, TerrainFoliage& foliage, std::span<const TerrainData::LodView> views,
    const std::function<void( Mesh*, const Matrix4& )>& submit )
{
    const auto& data = foliage.GetData(); const auto id = entity.GetId().Value(); foliage.m_drawCount = 0; foliage.m_renderError.clear();
    if( !std::isfinite( foliage.DrawDistance ) || foliage.DrawDistance <= 0 ) { foliage.m_renderError = "Foliage draw distance must be positive and finite"; RemoveFoliageDraw( id ); return; }
    auto old = m_foliageDraws.find( id );
    if( old != m_foliageDraws.end() )
    {
        bool changed = old->second.Sources.size() != data.Palette.size();
        if( !changed ) for( size_t i = 0; i < data.Palette.size(); ++i ) changed |= old->second.Sources[i].Path != data.Palette[i].Model;
        if( changed ) RemoveFoliageDraw( id );
    }
    auto& draw = m_foliageDraws[id];
    if( draw.Sources.empty() ) for( const auto& species : data.Palette )
    {
        FoliageDraw::Source source; source.Path = species.Model;
        const Path path( species.Model ); if( path.Exists ) source.Resource = ResourceCache::GetInstance().GetAsync<ModelResource>( path );
        draw.Sources.push_back( std::move( source ) );
    }
    const bool deterministic = CommandLine::GetFloat( "--frame-time", 0 ) > 0;
    for( auto& source : draw.Sources )
    {
        if( !source.Resource ) { foliage.m_renderError = "Missing foliage model: " + source.Path; continue; }
        if( deterministic && source.Resource->IsLoading() ) ResourceCache::GetInstance().CompleteLoad( source.Resource );
        if( source.Resource->IsLoading() ) { foliage.m_renderError = "Loading foliage model: " + source.Path; continue; }
        if( source.Resource->HasLoadFailed() ) { foliage.m_renderError = "Foliage model failed to load: " + source.Path; continue; }
        if( source.Prepared ) { if( !source.Error.empty() ) foliage.m_renderError = source.Error; continue; }
        source.Prepared = true;
        bool supported = source.Resource->GetAnimations().empty();
        for( const auto* mesh : source.Resource->GetAllMeshes() ) supported &= !mesh->IsSkinned() && !mesh->VertexCache;
        if( !supported ) { foliage.m_renderError = source.Error = "Foliage requires static models: " + source.Path; continue; }
        const auto collect = [&]( const auto& self, const Moonlight::Node& node, const glm::mat4& parent ) -> void {
            const glm::mat4 local = parent * glm::translate( glm::mat4( 1 ), node.Position.InternalVector ) * glm::mat4_cast( node.Rotation.InternalQuat ) * glm::scale( glm::mat4( 1 ), node.Scale.InternalVector );
            for( auto* mesh : node.Meshes ) source.Parts.push_back( { mesh, Matrix4( local ) } );
            for( const auto& child : node.Nodes ) self( self, child, local );
        };
        collect( collect, source.Resource->RootNode, glm::mat4( 1 ) );
        if( source.Parts.size() > 64 ) { source.Parts.clear(); foliage.m_renderError = source.Error = "Choose a foliage model with at most 64 mesh parts"; }
    }
    std::set<uint64_t> live; for( const auto& instance : data.Instances ) live.insert( instance.ID );
    for( auto it = draw.Instances.begin(); it != draw.Instances.end(); )
    {
        if( live.contains( it->first ) ) { ++it; continue; }
        for( const auto& mesh : it->second.Parts ) GetEngine().GetRenderer().GetMeshCache().Pop( mesh->GetId() );
        it = draw.Instances.erase( it );
    }
    const auto world = entity.GetComponent<Transform>().GetLocalToWorldMatrix().GetInternalMatrix(); size_t parts = 0;
    for( const auto& instance : data.Instances )
    {
        if( instance.SpeciesIndex >= draw.Sources.size() ) continue;
        const auto& source = draw.Sources[instance.SpeciesIndex]; if( source.Parts.empty() ) continue;
        const glm::vec3 position = glm::vec3( world * glm::vec4( instance.Position.InternalVector, 1 ) );
        if( !views.empty() && std::none_of( views.begin(), views.end(), [&]( const auto& view ) { return glm::distance( position, view.Position.InternalVector ) <= foliage.DrawDistance; } ) ) continue;
        if( parts + source.Parts.size() > 20000 ) { foliage.m_renderError = "Foliage draw limit reached (20000 mesh parts)"; break; } parts += source.Parts.size();
        auto found = draw.Instances.find( instance.ID );
        if( found != draw.Instances.end() && found->second.Species != instance.SpeciesIndex )
        {
            for( const auto& mesh : found->second.Parts ) GetEngine().GetRenderer().GetMeshCache().Pop( mesh->GetId() );
            draw.Instances.erase( found ); found = draw.Instances.end();
        }
        if( found == draw.Instances.end() )
        {
            FoliageDraw::Instance renderer; renderer.Species = instance.SpeciesIndex;
            for( const auto& prototype : source.Parts )
            {
                auto mesh = std::make_unique<Mesh>( prototype.Geometry ); mesh->Parent = entity.GetHandle();
                mesh->Id = GetEngine().GetRenderer().GetMeshCache().Push( Moonlight::MeshCommand() ); renderer.Parts.push_back( std::move( mesh ) );
            }
            found = draw.Instances.emplace( instance.ID, std::move( renderer ) ).first;
        }
        glm::quat alignment( 1, 0, 0, 0 );
        if( instance.AlignNormal ) alignment = instance.Normal.y < -0.9999f ? glm::quat( 0, 1, 0, 0 ) : glm::normalize( glm::quat( 1.f + instance.Normal.y, instance.Normal.z, 0, -instance.Normal.x ) );
        const auto rotation = alignment * glm::angleAxis( glm::radians( instance.Yaw ), glm::vec3( 0, 1, 0 ) );
        const glm::mat4 matrix = world * glm::translate( glm::mat4( 1 ), instance.Position.InternalVector ) * glm::mat4_cast( rotation ) * glm::scale( glm::mat4( 1 ), glm::vec3( instance.Scale ) );
        for( size_t p = 0; p < source.Parts.size(); ++p )
        {
            auto& mesh = found->second.Parts[p]; mesh->CastShadows = foliage.CastShadows; submit( mesh.get(), Matrix4( matrix * source.Parts[p].Local.GetInternalMatrix() ) ); ++foliage.m_drawCount;
        }
    }
    // Distance-hidden instances must not leave last frame's commands visible or casting shadows.
    for( const auto& [key, instance] : draw.Instances ) for( const auto& mesh : instance.Parts )
        if( auto* command = GetEngine().GetRenderer().GetMeshCache().Get( mesh->GetId() ) ) command->Visible = command->CastShadows = false;
}
