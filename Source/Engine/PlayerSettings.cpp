#include "PCH.h"
#include "PlayerSettings.h"
#include "Audio/AudioTypes.h"
#include "CLog.h"
#include "Core/CommandLine.h"
#include "Cores/AudioCore.h"
#include "Cores/UI/RmlUiCore.h"
#include "Engine/AutomationRunner.h"
#include "Engine/HardwareBudget.h"
#include "Input/InputActions.h"
#include "VFS/VFS.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

namespace
{
    // A changed value is written once nothing has changed for this long.
    constexpr double kSaveAfterSeconds = 0.5;

    constexpr const char* kBindingsKey = "Input.Bindings";
    constexpr const char* kGamePrefix = "Game.";


    double NowSeconds()
    {
        return std::chrono::duration<double>( std::chrono::steady_clock::now().time_since_epoch() ).count();
    }


    std::vector<std::string> SplitKey( const std::string& InKey )
    {
        std::vector<std::string> parts;
        size_t start = 0;
        while( start <= InKey.size() )
        {
            const size_t dot = InKey.find( '.', start );
            parts.push_back( InKey.substr( start, dot == std::string::npos ? std::string::npos : dot - start ) );
            if( dot == std::string::npos )
            {
                break;
            }
            start = dot + 1;
        }
        return parts;
    }


    const json* Find( const json& InRoot, const std::string& InKey )
    {
        const json* node = &InRoot;
        for( const std::string& part : SplitKey( InKey ) )
        {
            if( part.empty() || !node->is_object() )
            {
                return nullptr;
            }
            auto found = node->find( part );
            if( found == node->end() )
            {
                return nullptr;
            }
            node = &*found;
        }
        return node;
    }


    // The node at InKey, creating objects along the way (a non-object in the way is replaced).
    json& Make( json& InRoot, const std::string& InKey )
    {
        json* node = &InRoot;
        for( const std::string& part : SplitKey( InKey ) )
        {
            if( !node->is_object() )
            {
                *node = json::object();
            }
            node = &( *node )[part];
        }
        return *node;
    }


    std::optional<std::pair<double, double>> RangeOf( const std::string& InKey )
    {
        if( InKey.rfind( "Audio.", 0 ) == 0 )
        {
            return std::make_pair( 0.0, 1.0 );
        }
        if( InKey == "Accessibility.TextScale" )
        {
            return std::make_pair( 0.5, 3.0 );
        }
        return std::nullopt;
    }


    bool SameType( const json& InDefault, const json& InValue )
    {
        return ( InDefault.is_number() && InValue.is_number() ) || ( InDefault.is_string() && InValue.is_string() )
            || ( InDefault.is_boolean() && InValue.is_boolean() );
    }


    json Clamped( const std::string& InKey, const json& InValue )
    {
        const auto range = RangeOf( InKey );
        if( !range || !InValue.is_number() )
        {
            return InValue;
        }
        return std::clamp( InValue.get<double>(), range->first, range->second );
    }


    // Rebinds are control paths by key ("Gameplay/Jump/0" -> "Keyboard/J"); anything else drops.
    json BindingsFrom( const json& InValue )
    {
        json bindings = json::object();
        if( InValue.is_object() )
        {
            for( const auto& [key, control] : InValue.items() )
            {
                if( control.is_string() && InputActionSystem::IsValidControl( control.get<std::string>() ) )
                {
                    bindings[key] = control;
                }
            }
        }
        return bindings;
    }


    void Note( std::string* OutNotes, const std::string& InNote )
    {
        if( OutNotes )
        {
            *OutNotes += ( OutNotes->empty() ? "" : "; " ) + InNote;
        }
    }


    // Reads InStored's known keys into InOut (which holds the defaults), level by level.
    void MergeKnown( json& InOut, const json& InStored, const std::string& InPrefix, std::string* OutNotes )
    {
        for( auto& [key, fallback] : InOut.items() )
        {
            const std::string full = InPrefix + key;
            auto stored = InStored.find( key );
            if( stored == InStored.end() || full == "Version" )
            {
                continue;
            }
            if( full == "Game" )
            {
                if( stored->is_object() )
                {
                    fallback = *stored;
                }
                else
                {
                    Note( OutNotes, "Game isn't an object" );
                }
            }
            else if( full == kBindingsKey )
            {
                fallback = BindingsFrom( *stored );
                if( stored->is_object() && fallback.size() != stored->size() )
                {
                    Note( OutNotes, std::to_string( stored->size() - fallback.size() ) + " invalid rebinds dropped" );
                }
            }
            else if( fallback.is_object() )
            {
                if( stored->is_object() )
                {
                    MergeKnown( fallback, *stored, full + ".", OutNotes );
                }
                else
                {
                    Note( OutNotes, full + " isn't an object" );
                }
            }
            else if( SameType( fallback, *stored ) )
            {
                fallback = Clamped( full, *stored );
            }
            else
            {
                Note( OutNotes, full + " has the wrong type" );
            }
        }
        for( const auto& [key, value] : InStored.items() )
        {
            if( !InOut.contains( key ) )
            {
                Note( OutNotes, "unknown " + InPrefix + key + " dropped" );
            }
        }
    }


    // Version N's document becomes version N + 1's. None yet: version 1 is the first.
    using Migration = void ( * )( json& );
    constexpr Migration kMigrations[] = { nullptr };


    bool FindBus( const std::string& InName, AudioBus& OutBus )
    {
        for( int bus = 0; bus < static_cast<int>( AudioBus::Count ); ++bus )
        {
            if( InName == AudioBusName( static_cast<AudioBus>( bus ) ) )
            {
                OutBus = static_cast<AudioBus>( bus );
                return true;
            }
        }
        return false;
    }
}


PlayerSettings::PlayerSettings()
    : m_document( Defaults() )
{
}


PlayerSettings& PlayerSettings::Get()
{
    static PlayerSettings instance;
    return instance;
}


json PlayerSettings::Defaults()
{
    json audio = json::object();
    for( int bus = 0; bus < static_cast<int>( AudioBus::Count ); ++bus )
    {
        audio[AudioBusName( static_cast<AudioBus>( bus ) )] = 1.0;
    }
    return {
        { "Version", kVersion },
        { "Graphics", { { "Quality", "" } } },
        { "Audio", audio },
        { "Language", "" },
        { "Accessibility", { { "TextScale", 1.0 }, { "ReduceMotion", false } } },
        { "Input", { { "Bindings", json::object() } } },
        { "Game", json::object() },
    };
}


json PlayerSettings::Sanitize( const json& InStored, std::string* OutNotes )
{
    json result = Defaults();
    if( !InStored.is_object() )
    {
        Note( OutNotes, "not a settings document" );
        return result;
    }
    json stored = InStored;
    int version = stored.contains( "Version" ) && stored["Version"].is_number_integer() ? stored["Version"].get<int>() : kVersion;
    if( version > kVersion )
    {
        Note( OutNotes, "version " + std::to_string( version ) + " is newer than " + std::to_string( kVersion ) + ": reading the keys this build knows" );
    }
    for( ; version >= 1 && version < kVersion; ++version )
    {
        if( Migration migrate = kMigrations[version - 1] )
        {
            migrate( stored );
        }
    }
    MergeKnown( result, stored, "", OutNotes );
    return result;
}


void PlayerSettings::SetTargets( const Targets& InTargets )
{
    m_targets = InTargets;
}


void PlayerSettings::SetPath( const std::string& InPath )
{
    m_path = InPath;
}


void PlayerSettings::Init( const Targets& InTargets )
{
    SetTargets( InTargets );
    const bool persistent = !AutomationRunner::IsUnattendedRun() || CommandLine::Has( "--user-dir" );
    SetPath( persistent ? kDefaultPath : "" );
    Load();
}


bool PlayerSettings::Load()
{
    json stored = json::object();
    bool readable = true;
    std::string text;
    if( IsPersistent() && VFS::Get().ReadText( m_path, text ) )
    {
        stored = json::parse( text, nullptr, false );
        if( !stored.is_object() )
        {
            YIKES( "Settings: " + m_path + " isn't a settings document; using the defaults" );
            stored = json::object();
            readable = false;
        }
    }
    std::string notes;
    m_document = Sanitize( stored, &notes );
    if( !notes.empty() && readable )
    {
        BRUH( "Settings: " + m_path + ": " + notes );
    }
    Apply();
    m_dirty = false;
    ++m_revision;
    CLog::Log( CLog::LogType::Info, IsPersistent() ? "Settings: " + m_path + ( text.empty() ? " (defaults)" : "" ) : std::string( "Settings: in memory only (unattended run)" ) );
    return readable;
}


bool PlayerSettings::Save()
{
    if( m_targets.Actions )
    {
        Make( m_document, kBindingsKey ) = m_targets.Actions->SaveOverrides();
    }
    if( !IsPersistent() )
    {
        m_dirty = false;
        return false;
    }
    const std::string text = m_document.dump( 4 );
    const std::string temp = m_path + ".tmp";
    const VfsResolved target = VFS::Get().Resolve( m_path );
    const VfsResolved staged = VFS::Get().Resolve( temp );
    if( !VFS::Get().WriteText( temp, text ) )
    {
        YIKES( "Settings: can't write " + temp );
        return false;
    }
    // The rename replaces the old file in one step: a crash mid-write leaves the old settings.
    std::error_code error;
    std::filesystem::rename( staged.PhysicalPath, target.PhysicalPath, error );
    if( error )
    {
        std::filesystem::remove( staged.PhysicalPath, error );
        if( !VFS::Get().WriteText( m_path, text ) )
        {
            YIKES( "Settings: can't write " + m_path );
            return false;
        }
    }
    m_dirty = false;
    CLog::Log( CLog::LogType::Info, "Settings: saved " + m_path );
    return true;
}


void PlayerSettings::Reset()
{
    m_document = Defaults();
    Apply();
    MarkDirty();
}


void PlayerSettings::Update()
{
    if( m_dirty && NowSeconds() - m_changedAt >= kSaveAfterSeconds )
    {
        Save();
    }
}


json PlayerSettings::GetValue( const std::string& InKey ) const
{
    if( m_targets.Actions && InKey == kBindingsKey )
    {
        return m_targets.Actions->SaveOverrides();
    }
    if( InKey.rfind( "Input", 0 ) != 0 || !m_targets.Actions )
    {
        const json* value = Find( m_document, InKey );
        return value ? *value : json();
    }
    json document = m_document;
    Make( document, kBindingsKey ) = m_targets.Actions->SaveOverrides();
    const json* value = Find( document, InKey );
    return value ? *value : json();
}


bool PlayerSettings::SetValue( const std::string& InKey, const json& InValue, std::string* OutError )
{
    const auto fail = [&]( const std::string& InWhy ) {
        if( OutError )
        {
            *OutError = InWhy;
        }
        return false;
    };
    if( InKey.rfind( kGamePrefix, 0 ) == 0 )
    {
        for( const std::string& part : SplitKey( InKey ) )
        {
            if( part.empty() )
            {
                return fail( "'" + InKey + "' has an empty part" );
            }
        }
        Make( m_document, InKey ) = InValue;
        MarkDirty();
        return true;
    }
    const json defaults = Defaults();
    const json* fallback = InKey == "Version" ? nullptr : Find( defaults, InKey );
    if( !fallback )
    {
        return fail( "'" + InKey + "' isn't a setting (game values go under Game.)" );
    }
    if( InKey == kBindingsKey )
    {
        if( !InValue.is_object() )
        {
            return fail( std::string( kBindingsKey ) + " takes an object of rebinds" );
        }
        Make( m_document, InKey ) = BindingsFrom( InValue );
    }
    else if( fallback->is_object() )
    {
        return fail( "'" + InKey + "' is a group: set one of its keys" );
    }
    else if( !SameType( *fallback, InValue ) )
    {
        return fail( "'" + InKey + "' takes a " + std::string( fallback->type_name() ) + ", not " + InValue.dump() );
    }
    else
    {
        Make( m_document, InKey ) = Clamped( InKey, InValue );
    }
    ApplyKey( InKey );
    MarkDirty();
    return true;
}


bool PlayerSettings::Rebind( const std::string& InAction, size_t InBinding, const std::string& InPart, const std::string& InControl )
{
    if( !m_targets.Actions || !m_targets.Actions->SetBindingOverride( InAction, InBinding, InPart, InControl ) )
    {
        return false;
    }
    CaptureBindings();
    return true;
}


void PlayerSettings::ResetBindings()
{
    if( m_targets.Actions )
    {
        m_targets.Actions->ClearOverrides();
    }
    Make( m_document, kBindingsKey ) = json::object();
    MarkDirty();
}


void PlayerSettings::CaptureBindings()
{
    if( m_targets.Actions )
    {
        Make( m_document, kBindingsKey ) = m_targets.Actions->SaveOverrides();
        MarkDirty();
    }
}


void PlayerSettings::Apply()
{
    ApplyKey( "Graphics.Quality" );
    for( int bus = 0; bus < static_cast<int>( AudioBus::Count ); ++bus )
    {
        ApplyKey( std::string( "Audio." ) + AudioBusName( static_cast<AudioBus>( bus ) ) );
    }
    ApplyKey( "Language" );
    ApplyKey( "Accessibility.TextScale" );
    ApplyKey( kBindingsKey );
}


void PlayerSettings::ApplyKey( const std::string& InKey )
{
    const json* value = Find( m_document, InKey );
    if( !value )
    {
        return;
    }
    if( InKey == "Graphics.Quality" && m_targets.Budget && value->is_string() )
    {
        if( !m_targets.Budget->SetQualityPreset( value->get<std::string>() ) )
        {
            BRUH( "Settings: quality preset '" + value->get<std::string>() + "' isn't defined (QualityPresets in HardwareProfiles.json)" );
        }
    }
    else if( InKey.rfind( "Audio.", 0 ) == 0 && m_targets.Audio && value->is_number() )
    {
        AudioBus bus = AudioBus::Master;
        if( FindBus( InKey.substr( 6 ), bus ) )
        {
            m_targets.Audio->SetPlayerVolume( bus, value->get<float>() );
        }
    }
#if USING( ME_RMLUI )
    else if( InKey == "Language" && m_targets.UI && value->is_string() )
    {
        // A switch reloads every document: only when it changes.
        if( m_targets.UI->GetLanguage() != value->get<std::string>() )
        {
            m_targets.UI->SetLanguage( value->get<std::string>() );
        }
    }
    else if( InKey == "Accessibility.TextScale" && m_targets.UI && value->is_number() )
    {
        if( std::abs( m_targets.UI->GetTextScale() - value->get<float>() ) > 1e-4f )
        {
            m_targets.UI->SetTextScale( value->get<float>() );
        }
    }
#endif
    else if( InKey == kBindingsKey && m_targets.Actions )
    {
        m_targets.Actions->LoadOverrides( *value );
    }
}


void PlayerSettings::MarkDirty()
{
    m_dirty = true;
    m_changedAt = NowSeconds();
    ++m_revision;
}
