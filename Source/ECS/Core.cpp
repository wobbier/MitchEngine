#include "PCH.h"
#include "Core.h"
#include <imgui.h>
#include <string>
#include "Core/Assert.h"
#if defined( __GNUC__ ) || defined( __clang__ )
#include <cxxabi.h>
#endif

static std::string CleanTypeName( const char* rawName )
{
#if defined( __GNUC__ ) || defined( __clang__ )
    int status = 0;
    char* demangled = abi::__cxa_demangle( rawName, nullptr, nullptr, &status );
    std::string result = ( status == 0 && demangled ) ? demangled : rawName;
    free( demangled );
    
    auto pos = result.rfind( "::" );
    if( pos != std::string::npos )
        result = result.substr( pos + 2 );
    return result;
#else
    std::string name( rawName );
    auto pos = name.find( ' ' );
    return pos != std::string::npos ? name.substr( pos + 1 ) : name;
#endif
}

BaseCore::BaseCore( const char* CompName, const ComponentFilter& Filter )
    : Name( CleanTypeName( CompName ) )
    , CompFilter( Filter )
{
}

World& BaseCore::GetWorld() const
{
    ME_ASSERT_MSG( GameWorld, "World is null." );
    return *GameWorld;
}

const std::vector<Entity>& BaseCore::GetEntities() const
{
    return Entities;
}

std::vector<Entity>& BaseCore::GetEntities()
{
    return Entities;
}

const ComponentFilter& BaseCore::GetComponentFilter() const
{
    return CompFilter;
}

const std::string& BaseCore::GetName() const
{
    return Name;
}

bool BaseCore::Contains( uint32_t InEntityIndex ) const
{
    return InEntityIndex < m_memberSlots.size() && m_memberSlots[InEntityIndex] != 0;
}

void BaseCore::Add( Entity& InEntity )
{
    const uint32_t index = InEntity.GetId().Index;
    if( Contains( index ) )
    {
        return;
    }
    if( index >= m_memberSlots.size() )
    {
        m_memberSlots.resize( index + 1, 0 );
    }
    Entities.push_back( InEntity );
    m_memberSlots[index] = static_cast<uint32_t>( Entities.size() );
    OnEntityAdded( Entities.back() );
}

void BaseCore::Remove( Entity& InEntity )
{
    const uint32_t index = InEntity.GetId().Index;
    if( !Contains( index ) )
    {
        return;
    }

    // Copy first: InEntity may refer to an element of Entities, which the swap-remove overwrites.
    Entity removed = InEntity;
    OnEntityRemoved( removed );

    const uint32_t position = m_memberSlots[index] - 1;
    const uint32_t last = static_cast<uint32_t>( Entities.size() - 1 );
    if( position != last )
    {
        Entities[position] = Entities[last];
        m_memberSlots[Entities[position].GetId().Index] = position + 1;
    }
    Entities.pop_back();
    m_memberSlots[index] = 0;
}

void BaseCore::Clear()
{
    Entities.clear();
    m_memberSlots.clear();
}

const bool BaseCore::GetIsSerializable() const
{
    return IsSerializable;
}

void BaseCore::SetIsSerializable( bool value )
{
    IsSerializable = value;
}

#if USING( ME_EDITOR )

void BaseCore::OnEditorInspect()
{
    ImGui::Text( "Entity Count: %i", Entities.size() );
    ImGui::Checkbox( "Destroy On Load", &DestroyOnLoad );
}

#endif