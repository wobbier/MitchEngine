#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Path.h"

#ifdef FMOD_ENABLED
#include "fmod.hpp"
namespace FMOD { class System; }
#endif

class AudioSource
	: public Component<AudioSource>
{
public:
	AudioSource();
	AudioSource(const std::string& InFilePath);

	void Play(const bool ShouldLoop = false);
	void Stop(bool immediate = true);
	bool Preload = false;

	bool IsPlaying() const;

#if ME_EDITOR
	virtual void OnEditorInspect() override;
#endif
	virtual void Init() override;

	void ClearData();

	bool IsInitialized = false;

	bool PlayOnAwake = false;
	bool Loop = false;

#ifdef FMOD_ENABLED
	FMOD::Sound* SoundInstance;
	FMOD::Channel* ChannelHandle;
#endif

	Path FilePath;

private:
	virtual void OnSerialize(json& outJson) final;
	virtual void OnDeserialize(const json& inJson) final;

#ifdef FMOD_ENABLED
	FMOD::System* m_owner = nullptr;
#endif
};

ME_REGISTER_COMPONENT_FOLDER(AudioSource, "Audio")