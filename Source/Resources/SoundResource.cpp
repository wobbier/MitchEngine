#include "PCH.h"
#include "SoundResource.h"

#if USING( ME_FMOD )
#include <fmod.hpp>
#endif

Sound::Sound( const Path& path, void* fmodSystem, bool isImmediate )
    : Resource( path )
{
#if USING( ME_FMOD )
    FMOD::System* system = static_cast<FMOD::System*>( fmodSystem );
    if( !system )
    {
        YIKES( "FMOD System is not enabled." );
    }
    if( system->createSound( path.FullPath.c_str(), FMOD_DEFAULT | ( isImmediate ? FMOD_DEFAULT : FMOD_NONBLOCKING ), nullptr, &Handle ) != FMOD_OK )
    {
        // #TODO: Perhaps having the macro for this accepts a string view?
        YIKES( "Failed to create sound resource: " + path.FullPath );
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
    bool result = Handle->getOpenState( &openstate, nullptr, nullptr, nullptr );
    return openstate == FMOD_OPENSTATE_READY;
#else
    return true;
#endif
}
