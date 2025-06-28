#include "PCH.h"
#include "SoundResource.h"

#if USING( ME_FMOD )
#include <fmod.hpp>
#endif

Sound::Sound( const Path& path, void* fmodSystem, SoundFlags inFlags )
    : Resource( path )
{
#if USING( ME_FMOD )
    FMOD::System* system = static_cast<FMOD::System*>( fmodSystem );
    if( !system )
    {
        YIKES( "FMOD System is not enabled." );
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
    if( !system )
    {
        YIKES( "FMOD System is not enabled." );
    }

    FMOD_CREATESOUNDEXINFO exinfo;
    memset( &exinfo, 0, sizeof( FMOD_CREATESOUNDEXINFO ) );
    exinfo.cbsize = sizeof( FMOD_CREATESOUNDEXINFO );
    exinfo.filebuffersize = 1024 * 16;        /* Increase the default file chunk size to handle seeking inside large playlist files that may be over 2kb. */
    if( system->createSound( url, SoundFlags::NonBlocking | SoundFlags::CreateStream, &exinfo, &Handle ) != FMOD_OK )
    {
        // #TODO: Perhaps having the macro for this accepts a string view?
        YIKES( "Failed to create sound resource: {}", url );
    }
#endif
}


Sound::~Sound()
{
#if USING( ME_FMOD )
    if( Handle )
    {
        Handle->release();
        Handle = nullptr;
    }
#endif
}


bool Sound::IsReady() const
{
#if USING( ME_FMOD )
    FMOD_OPENSTATE openstate;
    unsigned int    percent = 0;
    bool            starving = false;
    bool result = Handle->getOpenState( &openstate, &percent, &starving, nullptr );
    return openstate == FMOD_OPENSTATE_READY || openstate == FMOD_OPENSTATE_PLAYING;
#else
    return true;
#endif
}
