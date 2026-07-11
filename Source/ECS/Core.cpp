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

void BaseCore::Add( Entity& InEntity )
{
    Entities.push_back( InEntity );
    OnEntityAdded( InEntity );
}

void BaseCore::Remove( Entity& InEntity )
{
    OnEntityRemoved( InEntity );
    Entities.erase( std::remove( Entities.begin(), Entities.end(), InEntity ), Entities.end() );
}

void BaseCore::Clear()
{
    Entities.clear();
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