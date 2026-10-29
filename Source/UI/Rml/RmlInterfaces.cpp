#include "PCH.h"
#include "UI/Rml/RmlInterfaces.h"

#if USING( ME_RMLUI )

#include "CLog.h"
#include "Engine/Localization.h"
#include "Path.h"
#include "Resource/ResourceCache.h"
#include "VFS/VFS.h"

double RmlSystemInterface::GetElapsedTime()
{
    return m_elapsedTime;
}


int RmlSystemInterface::TranslateString( Rml::String& OutTranslated, const Rml::String& InInput )
{
    // Text nodes keep their markup whitespace; keys match the trimmed text.
    return Localization::Get().Translate( InInput, OutTranslated ) ? 1 : 0;
}


void RmlSystemInterface::AdvanceTime( double InDeltaSeconds )
{
    m_elapsedTime += InDeltaSeconds;
}


void RmlSystemInterface::SetMouseCursor( const Rml::String& InCursorName )
{
    m_cursor = InCursorName;
}


bool RmlSystemInterface::LogMessage( Rml::Log::Type InType, const Rml::String& InMessage )
{
    switch( InType )
    {
    case Rml::Log::LT_ERROR:
    case Rml::Log::LT_ASSERT:
        YIKES( "[UI] " + InMessage );
        break;
    case Rml::Log::LT_WARNING:
        BRUH( "[UI] " + InMessage );
        break;
    default:
        INFO( "UI", InMessage );
        break;
    }
    return true;
}


std::map<std::string, std::string> RmlFileInterface::s_overrides;


void RmlFileInterface::SetOverride( const std::string& InPath, const std::string& InText )
{
    s_overrides[ResourceCache::CacheKey( InPath )] = InText;
}


void RmlFileInterface::ClearOverride( const std::string& InPath )
{
    s_overrides.erase( ResourceCache::CacheKey( InPath ) );
}


Rml::FileHandle RmlFileInterface::Open( const Rml::String& InPath )
{
    const Path resolved( InPath );
    if( !s_overrides.empty() )
    {
        const auto found = s_overrides.find( ResourceCache::CacheKey( resolved ) );
        if( found != s_overrides.end() )
        {
            auto* file = new OpenFile();
            file->Bytes.assign( found->second.begin(), found->second.end() );
            return reinterpret_cast<Rml::FileHandle>( file );
        }
    }
    if( !resolved.Exists )
    {
        return 0;
    }
    auto* file = new OpenFile();
    if( !VFS::Get().ReadBytes( resolved, file->Bytes ) )
    {
        delete file;
        return 0;
    }
    return reinterpret_cast<Rml::FileHandle>( file );
}


void RmlFileInterface::Close( Rml::FileHandle InFile )
{
    delete reinterpret_cast<OpenFile*>( InFile );
}


size_t RmlFileInterface::Read( void* OutBuffer, size_t InSize, Rml::FileHandle InFile )
{
    OpenFile* file = reinterpret_cast<OpenFile*>( InFile );
    const size_t remaining = file->Bytes.size() - file->Position;
    const size_t count = InSize < remaining ? InSize : remaining;
    if( count > 0 )
    {
        memcpy( OutBuffer, file->Bytes.data() + file->Position, count );
        file->Position += count;
    }
    return count;
}


bool RmlFileInterface::Seek( Rml::FileHandle InFile, long InOffset, int InOrigin )
{
    OpenFile* file = reinterpret_cast<OpenFile*>( InFile );
    long long target = 0;
    switch( InOrigin )
    {
    case SEEK_SET: target = InOffset; break;
    case SEEK_CUR: target = static_cast<long long>( file->Position ) + InOffset; break;
    case SEEK_END: target = static_cast<long long>( file->Bytes.size() ) + InOffset; break;
    default: return false;
    }
    if( target < 0 || target > static_cast<long long>( file->Bytes.size() ) )
    {
        return false;
    }
    file->Position = static_cast<size_t>( target );
    return true;
}


size_t RmlFileInterface::Tell( Rml::FileHandle InFile )
{
    return reinterpret_cast<OpenFile*>( InFile )->Position;
}

#endif
