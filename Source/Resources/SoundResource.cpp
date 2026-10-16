#include "PCH.h"
#include "SoundResource.h"

#include <chrono>
#include <thread>

#if USING( ME_FMOD )
#include <fmod.hpp>
#endif

Sound::Sound( const Path& path, void* fmodSystem, SoundFlags inFlags )
    : Resource( path )
{
#if USING( ME_FMOD )
    FMOD::System* system = static_cast<FMOD::System*>( fmodSystem );
    System = system;
    if( !system )
    {
        YIKES( "FMOD System is not enabled." );
        return;
    }

    if( system->createSound( path.FullPath.c_str(), inFlags, nullptr, &Handle ) != FMOD_OK )
    {
        // #TODO: Perhaps having the macro for this accepts a string view?
        YIKES( "Failed to create sound resource: " + path.FullPath );
    }
#endif
}


Sound::Sound( const char* url, void* fmodSystem /*= nullptr */ )
    : Resource( Path() )
{
#if USING( ME_FMOD )
    FMOD::System* system = static_cast<FMOD::System*>( fmodSystem );
    System = system;
    if( !system )
    {
        YIKES( "FMOD System is not enabled." );
        return;
    }

    FMOD_CREATESOUNDEXINFO exinfo;
    memset( &exinfo, 0, sizeof( FMOD_CREATESOUNDEXINFO ) );
    exinfo.cbsize = sizeof( FMOD_CREATESOUNDEXINFO );
    exinfo.filebuffersize = 1024 * 16;        /* Increase the default file chunk size to handle seeking inside large playlist files that may be over 2kb. */
    if( system->createSound( url, SoundFlags::NonBlocking | SoundFlags::CreateStream, &exinfo, &Handle ) != FMOD_OK )
    {
        // #TODO: Perhaps having the macro for this accepts a string view?
        YIKES_FMT( "Failed to create sound resource: %s", url );
    }
#endif
}


Sound::~Sound()
{
    Release();
}


void Sound::Release()
{
#if USING( ME_FMOD )
    if( Handle )
    {
        Handle->release();
        Handle = nullptr;
    }
#endif
}


bool Sound::IsStream() const
{
#if USING( ME_FMOD )
    FMOD_MODE mode = 0;
    return Handle && Handle->getMode( &mode ) == FMOD_OK && ( mode & FMOD_CREATESTREAM ) != 0;
#else
    return false;
#endif
}


bool Sound::IsReady() const
{
#if USING( ME_FMOD )
    if( !Handle )
    {
        return false;
    }
    FMOD_OPENSTATE openstate = FMOD_OPENSTATE_ERROR;
    if( Handle->getOpenState( &openstate, nullptr, nullptr, nullptr ) != FMOD_OK )
    {
        return false;
    }
    // Buffering / seeking streams can still be played; only an unfinished or failed open can't.
    return openstate != FMOD_OPENSTATE_LOADING && openstate != FMOD_OPENSTATE_CONNECTING && openstate != FMOD_OPENSTATE_ERROR;
#else
    return true;
#endif
}


bool Sound::HasFailed() const
{
#if USING( ME_FMOD )
    if( !Handle )
    {
        return true;
    }
    FMOD_OPENSTATE openstate = FMOD_OPENSTATE_ERROR;
    return Handle->getOpenState( &openstate, nullptr, nullptr, nullptr ) != FMOD_OK || openstate == FMOD_OPENSTATE_ERROR;
#else
    return false;
#endif
}


bool Sound::WaitUntilReady() const
{
    while( !IsReady() )
    {
        if( HasFailed() )
        {
            return false;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }
    return true;
}
