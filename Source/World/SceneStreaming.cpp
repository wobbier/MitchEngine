#include "PCH.h"
#include "SceneStreaming.h"
#include "CLog.h"
#include "Engine/World.h"
#include "File.h"
#include "Path.h"
#include "World/SceneSerializer.h"
#include "optick.h"
#include <algorithm>
#include <chrono>


SceneStreaming::~SceneStreaming()
{
    Reset();
}


uint16_t SceneStreaming::NextId()
{
    // Ids are never reused while a scene with that id is known.
    do
    {
        if( ++m_nextId == 0 )
        {
            m_nextId = 1;
        }
    } while( Find( m_nextId ) );
    return m_nextId;
}


SceneStreaming::Entry* SceneStreaming::Find( uint16_t InId )
{
    auto it = std::find_if( m_scenes.begin(), m_scenes.end(), [InId]( const Entry& InEntry ) { return InEntry.Id == InId; } );
    return it == m_scenes.end() ? nullptr : &*it;
}


const SceneStreaming::Entry* SceneStreaming::Find( uint16_t InId ) const
{
    auto it = std::find_if( m_scenes.begin(), m_scenes.end(), [InId]( const Entry& InEntry ) { return InEntry.Id == InId; } );
    return it == m_scenes.end() ? nullptr : &*it;
}


json SceneStreaming::ReadScene( const std::string& InPath )
{
    OPTICK_EVENT( "SceneStreaming::ReadScene" );
    File file{ Path( InPath ) };
    file.Read();
    if( file.Data.empty() )
    {
        return json();
    }
    json data = json::parse( file.Data, nullptr, false );
    if( data.is_discarded() || !data.is_object() )
    {
        return json();
    }
    return SceneSerializer::MigrateToLatest( data );
}


void SceneStreaming::Instantiate( World& InWorld, Entry& InEntry, json InData )
{
    OPTICK_EVENT( "SceneStreaming::Instantiate" );
    if( InData.is_null() )
    {
        InEntry.Status = State::Failed;
        YIKES( "Additive scene couldn't be loaded: " + InEntry.Path );
        return;
    }
    // Main thread: the asset database may scan on first use.
    SceneSerializer::RemapAssetReferences( InData, InEntry.Path );
    // The same scene loaded twice (or GUIDs that collide with the world's) gets fresh GUIDs, with
    // its internal references remapped.
    bool collides = false;
    if( InData.contains( "Entities" ) )
    {
        for( const json& entity : InData["Entities"] )
        {
            const uint64_t guid = SceneSerializer::GUIDFromJson( entity.value( "GUID", json() ) );
            if( guid != 0 && InWorld.FindEntityByGUID( guid ) )
            {
                collides = true;
                break;
            }
        }
    }
    SceneSerializer::LoadOptions options;
    options.LoadCores = true;
    options.RemapGUIDs = collides;
    const bool wasLoading = InWorld.IsLoading;
    const uint16_t previousScene = InWorld.GetLoadingScene();
    InWorld.IsLoading = true;   // no sync points while it's half built
    InWorld.SetLoadingScene( InEntry.Id );
    SceneSerializer::Deserialize( InWorld, InData, options );
    InWorld.SetLoadingScene( previousScene );
    InWorld.IsLoading = wasLoading;
    InEntry.Status = State::Loaded;
    CLog::Log( CLog::LogType::Info, "Additive scene loaded: " + InEntry.Path + " (id " + std::to_string( InEntry.Id ) + ")" );
}


uint16_t SceneStreaming::Load( World& InWorld, const std::string& InPath )
{
    Entry entry;
    entry.Id = NextId();
    entry.Path = InPath;
    m_scenes.push_back( std::move( entry ) );
    Entry& added = m_scenes.back();
    Instantiate( InWorld, added, ReadScene( InPath ) );
    if( added.Status == State::Failed )
    {
        m_scenes.pop_back();
        return 0;
    }
    return added.Id;
}


uint16_t SceneStreaming::LoadAsync( const std::string& InPath )
{
    Entry entry;
    entry.Id = NextId();
    entry.Path = InPath;
    entry.Status = State::Loading;
    entry.Pending = std::async( std::launch::async, [InPath] { return ReadScene( InPath ); } );
    m_scenes.push_back( std::move( entry ) );
    return m_scenes.back().Id;
}


std::vector<uint16_t> SceneStreaming::Pump( World& InWorld )
{
    std::vector<uint16_t> loaded;
    for( Entry& entry : m_scenes )
    {
        if( entry.Status != State::Loading )
        {
            continue;
        }
        if( entry.Pending.wait_for( std::chrono::seconds( 0 ) ) != std::future_status::ready )
        {
            break;   // keep request order: later scenes wait for this one
        }
        Instantiate( InWorld, entry, entry.Pending.get() );
        if( entry.Status == State::Loaded )
        {
            loaded.push_back( entry.Id );
        }
    }
    return loaded;
}


std::vector<uint16_t> SceneStreaming::Wait( World& InWorld )
{
    for( Entry& entry : m_scenes )
    {
        if( entry.Status == State::Loading )
        {
            entry.Pending.wait();
        }
    }
    return Pump( InWorld );
}


bool SceneStreaming::Unload( World& InWorld, uint16_t InId )
{
    Entry* entry = Find( InId );
    if( !entry )
    {
        return false;
    }
    if( entry->Status == State::Loaded )
    {
        const size_t destroyed = InWorld.DestroyScene( InId );
        CLog::Log( CLog::LogType::Info, "Additive scene unloaded: " + entry->Path + " (" + std::to_string( destroyed ) + " entities)" );
    }
    // A pending read finishes (the future's destructor waits) and is discarded.
    m_scenes.erase( m_scenes.begin() + ( entry - m_scenes.data() ) );
    return true;
}


void SceneStreaming::Reset()
{
    m_scenes.clear();
}


SceneStreaming::State SceneStreaming::GetState( uint16_t InId ) const
{
    const Entry* entry = Find( InId );
    return entry ? entry->Status : State::None;
}


std::string SceneStreaming::GetPath( uint16_t InId ) const
{
    const Entry* entry = Find( InId );
    return entry ? entry->Path : std::string();
}


std::vector<uint16_t> SceneStreaming::GetLoaded() const
{
    std::vector<uint16_t> ids;
    for( const Entry& entry : m_scenes )
    {
        if( entry.Status == State::Loaded )
        {
            ids.push_back( entry.Id );
        }
    }
    return ids;
}
