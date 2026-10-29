#include "PCH.h"
#include "SaveGames.h"
#include "CLog.h"
#include "Components/Scripting/ScriptComponent.h"
#include "Cores/PhysicsCore.h"
#include "Cores/SceneCore.h"
#include "Engine/Engine.h"
#include "Engine/ProjectSettings.h"
#include "Renderer.h"
#include "VFS/VFS.h"
#include "World/Scene.h"
#include "World/SceneSerializer.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>

namespace
{
    // Version N's document becomes version N + 1's. None yet: version 1 is the first.
    using Migration = void ( * )( json& );
    constexpr Migration kMigrations[] = { nullptr };


    std::string PhysicalPath( const std::string& InLogicalPath )
    {
        return VFS::Get().Resolve( InLogicalPath ).PhysicalPath;
    }


    // Writes InText next to InLogicalPath, then renames it over the file: a crash mid-write
    // leaves the previous file whole. Safe off the main thread (VFS reads a mount snapshot).
    bool WriteAtomically( const std::string& InLogicalPath, const std::string& InText )
    {
        const std::string temp = InLogicalPath + ".tmp";
        if( !VFS::Get().WriteText( temp, InText ) )
        {
            return false;
        }
        std::error_code error;
        std::filesystem::rename( PhysicalPath( temp ), PhysicalPath( InLogicalPath ), error );
        if( error )
        {
            std::filesystem::remove( PhysicalPath( temp ), error );
            return VFS::Get().WriteText( InLogicalPath, InText );
        }
        return true;
    }


    std::string NowUtc()
    {
        const std::time_t now = std::chrono::system_clock::to_time_t( std::chrono::system_clock::now() );
        std::tm utc{};
#if USING( ME_PLATFORM_WINDOWS )
        gmtime_s( &utc, &now );
#else
        gmtime_r( &now, &utc );
#endif
        char text[32];
        std::strftime( text, sizeof( text ), "%Y-%m-%dT%H:%M:%SZ", &utc );
        return text;
    }


    bool IsPlaying()
    {
        // The world's cores run between Play and Stop (always, in a game build).
        return GetEngine().Physics != nullptr && GetEngine().Physics->IsStarted();
    }
}


json SaveGames::SlotInfo::ToJson() const
{
    return { { "Slot", Slot }, { "Name", Name }, { "Scene", Scene }, { "Date", Date }, { "Playtime", Playtime }, { "Version", Version } };
}


SaveGames::SlotInfo SaveGames::SlotInfo::FromJson( const std::string& InSlot, const json& InMeta )
{
    SlotInfo info;
    info.Slot = InSlot;
    if( InMeta.is_object() )
    {
        info.Name = InMeta.value( "Name", std::string() );
        info.Scene = InMeta.value( "Scene", std::string() );
        info.Date = InMeta.value( "Date", std::string() );
        info.Playtime = InMeta.contains( "Playtime" ) && InMeta["Playtime"].is_number() ? InMeta["Playtime"].get<double>() : 0.0;
        info.Version = InMeta.contains( "Version" ) && InMeta["Version"].is_number_integer() ? InMeta["Version"].get<int>() : kVersion;
    }
    return info;
}


SaveGames& SaveGames::Get()
{
    static SaveGames instance;
    return instance;
}


bool SaveGames::IsValidSlot( const std::string& InSlot )
{
    if( InSlot.empty() || InSlot.size() > 64 )
    {
        return false;
    }
    return std::all_of( InSlot.begin(), InSlot.end(), []( char c ) {
        return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '-' || c == '_';
    } );
}


std::string SaveGames::GetSlotDirectory( const std::string& InSlot )
{
    return std::string( kRoot ) + "/" + InSlot;
}


json SaveGames::Migrate( const json& InSave, std::string* OutNotes )
{
    json save = InSave;
    int version = save.contains( "Version" ) && save["Version"].is_number_integer() ? save["Version"].get<int>() : 1;
    if( version > kVersion && OutNotes )
    {
        *OutNotes = "version " + std::to_string( version ) + " is newer than " + std::to_string( kVersion ) + ": loading what this build knows";
    }
    for( ; version >= 1 && version < kVersion; ++version )
    {
        if( Migration migrate = kMigrations[version - 1] )
        {
            migrate( save );
        }
    }
    save["Version"] = std::max( version, kVersion );
    if( save.contains( "World" ) )
    {
        save["World"] = SceneSerializer::MigrateToLatest( save["World"] );
    }
    return save;
}


bool SaveGames::WriteSlot( const std::string& InSlotDirectory, const json& InMeta, const json& InWorld )
{
    json meta = InMeta;
    meta["Version"] = kVersion;
    const json save = { { "Version", kVersion }, { "Meta", meta }, { "World", InWorld } };
    // Save.json is the save; Meta.json, written last, is what listings read.
    return WriteAtomically( InSlotDirectory + "/Save.json", save.dump() ) && WriteAtomically( InSlotDirectory + "/Meta.json", meta.dump( 4 ) );
}


bool SaveGames::ReadSlot( const std::string& InSlotDirectory, json& OutMeta, json& OutWorld, std::string* OutError )
{
    const auto fail = [&]( const std::string& InWhy ) {
        if( OutError )
        {
            *OutError = InWhy;
        }
        return false;
    };
    std::string text;
    if( !VFS::Get().ReadText( InSlotDirectory + "/Save.json", text ) )
    {
        return fail( "no save in " + InSlotDirectory );
    }
    const json stored = json::parse( text, nullptr, false );
    if( !stored.is_object() || !stored.contains( "World" ) || !stored["World"].is_object() )
    {
        return fail( InSlotDirectory + "/Save.json isn't a save" );
    }
    std::string notes;
    json save = Migrate( stored, &notes );
    if( !notes.empty() )
    {
        BRUH( "SaveGame: " + InSlotDirectory + ": " + notes );
    }
    OutMeta = save.contains( "Meta" ) && save["Meta"].is_object() ? save["Meta"] : json::object();
    OutWorld = std::move( save["World"] );
    return true;
}


bool SaveGames::Save( const std::string& InSlot, const std::string& InName )
{
    if( !IsValidSlot( InSlot ) )
    {
        YIKES( "SaveGame: '" + InSlot + "' isn't a slot name (letters, digits, - and _)" );
        return false;
    }
    Engine& engine = GetEngine();
    SharedPtr<World> world = engine.GetWorld().lock();
    if( !world || !engine.SceneNodes || !engine.CurrentScene )
    {
        YIKES( "SaveGame: no scene to save" );
        return false;
    }
    WaitForWrites();

    // Script fields marked [Save] ride along with the public ones in this snapshot only.
    ScriptComponent::s_includeSaveFields = true;
    json worldData = SceneSerializer::SerializeWorld( *world, engine.SceneNodes->GetRootTransform() );
    ScriptComponent::s_includeSaveFields = false;

    const json meta = {
        { "Name", InName.empty() ? InSlot : InName },
        { "Scene", engine.CurrentScene->FilePath.GetLocalPathString() },
        { "Date", NowUtc() },
        { "Playtime", m_playtime },
        { "ProductVersion", ProjectSettings::Get().ProductVersion },
    };
    const std::string directory = GetSlotDirectory( InSlot );
    m_writingSlot = InSlot;
    m_write = std::async( std::launch::async, [directory, meta, world = std::move( worldData )]() {
        return WriteSlot( directory, meta, world );
    } );
    // The frame the save was taken on, written by the renderer at the end of this frame.
    const std::string thumbnail = PhysicalPath( directory + "/Thumbnail.png" );
    std::error_code error;
    std::filesystem::create_directories( std::filesystem::path( thumbnail ).parent_path(), error );
    engine.GetRenderer().RequestScreenshot( thumbnail );
    CLog::Log( CLog::LogType::Info, "SaveGame: saving " + InSlot + " (" + meta["Scene"].get<std::string>() + ")" );
    return true;
}


bool SaveGames::Load( const std::string& InSlot )
{
    if( !IsValidSlot( InSlot ) || !Exists( InSlot ) )
    {
        YIKES( "SaveGame: no save in slot '" + InSlot + "'" );
        return false;
    }
    m_pendingLoad = InSlot;
    return true;
}


bool SaveGames::Delete( const std::string& InSlot )
{
    if( !IsValidSlot( InSlot ) || !Exists( InSlot ) )
    {
        return false;
    }
    if( m_writingSlot == InSlot )
    {
        WaitForWrites();
    }
    std::error_code error;
    std::filesystem::remove_all( PhysicalPath( GetSlotDirectory( InSlot ) ), error );
    CLog::Log( CLog::LogType::Info, "SaveGame: deleted " + InSlot );
    return !error;
}


bool SaveGames::Exists( const std::string& InSlot ) const
{
    return IsValidSlot( InSlot ) && VFS::Get().Resolve( GetSlotDirectory( InSlot ) + "/Save.json" ).Exists;
}


bool SaveGames::ReadInfo( const std::string& InSlot, SlotInfo& OutInfo ) const
{
    if( !Exists( InSlot ) )
    {
        return false;
    }
    std::string text;
    json meta;
    if( VFS::Get().ReadText( GetSlotDirectory( InSlot ) + "/Meta.json", text ) )
    {
        meta = json::parse( text, nullptr, false );
    }
    if( !meta.is_object() )
    {
        // An older Meta.json (or none): the save itself has it.
        json world;
        if( !ReadSlot( GetSlotDirectory( InSlot ), meta, world ) )
        {
            return false;
        }
    }
    OutInfo = SlotInfo::FromJson( InSlot, meta );
    return true;
}


std::vector<SaveGames::SlotInfo> SaveGames::List() const
{
    std::vector<SlotInfo> slots;
    VFS::Get().Enumerate( kRoot, false, [&]( const std::string& InPath, bool InIsDirectory ) {
        if( !InIsDirectory )
        {
            return;
        }
        const size_t slash = InPath.find_last_of( '/' );
        SlotInfo info;
        if( ReadInfo( InPath.substr( slash == std::string::npos ? 0 : slash + 1 ), info ) )
        {
            slots.push_back( std::move( info ) );
        }
    } );
    std::sort( slots.begin(), slots.end(), []( const SlotInfo& InA, const SlotInfo& InB ) { return InA.Date > InB.Date; } );
    return slots;
}


bool SaveGames::IsWriting() const
{
    return m_write.valid() && m_write.wait_for( std::chrono::seconds( 0 ) ) != std::future_status::ready;
}


bool SaveGames::WaitForWrites()
{
    if( !m_write.valid() )
    {
        return true;
    }
    const bool written = m_write.get();
    if( written )
    {
        CLog::Log( CLog::LogType::Info, "SaveGame: saved " + m_writingSlot );
    }
    else
    {
        YIKES( "SaveGame: couldn't write " + GetSlotDirectory( m_writingSlot ) );
    }
    m_writingSlot.clear();
    return written;
}


void SaveGames::Update( float InUnscaledDeltaSeconds )
{
    if( IsPlaying() )
    {
        m_playtime += InUnscaledDeltaSeconds;
    }
    if( m_write.valid() && !IsWriting() )
    {
        WaitForWrites();
    }
    if( m_pendingLoad.empty() )
    {
        return;
    }
    const std::string slot = std::move( m_pendingLoad );
    m_pendingLoad.clear();
    if( m_writingSlot == slot )
    {
        WaitForWrites();
    }
    json meta;
    json world;
    std::string error;
    if( !ReadSlot( GetSlotDirectory( slot ), meta, world, &error ) )
    {
        YIKES( "SaveGame: " + error );
        return;
    }
    const SlotInfo info = SlotInfo::FromJson( slot, meta );
    m_playtime = info.Playtime;
    GetEngine().LoadSceneFromData( world, info.Scene );
    CLog::Log( CLog::LogType::Info, "SaveGame: loaded " + slot + " (" + info.Scene + ")" );
}
