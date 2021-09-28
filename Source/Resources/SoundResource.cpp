#include "PCH.h"
#include "SoundResource.h"

#ifdef FMOD_ENABLED
#include <fmod.hpp>
#endif

Sound::Sound(const Path& path, void* fmodSystem)
	: Resource(path)
{
#ifdef FMOD_ENABLED
	FMOD::System* system = static_cast<FMOD::System*>(fmodSystem);
	if(!system)
	{
		YIKES("FMOD System is not enabled.");
	}
	if(system->createSound(path.FullPath.c_str(), FMOD_DEFAULT, nullptr, &Handle) != FMOD_OK)
	{
		YIKES("Failed to create sound resource." + path.LocalPath);
	}
#endif
}
