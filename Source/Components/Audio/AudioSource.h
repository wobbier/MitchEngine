#pragma once
#include <Audio.h>
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Path.h"

class AudioSource
	: public Component<AudioSource>
{
public:
	AudioSource();
	AudioSource(const std::string& InFilePath);

	bool Load();
	void Play(const bool ShouldLoop = false);
	void Stop(bool immediate = true);
	bool Preload = false;

#if ME_EDITOR
	virtual void OnEditorInspect() override;
#endif
	virtual void Init() override;

	bool IsInitialized = false;

	bool PlayOnAwake = false;
	bool Loop = false;

	std::unique_ptr<DirectX::SoundEffect> SoundEffectFile;
	std::unique_ptr<DirectX::SoundEffectInstance> SoundInstance;

	Path FilePath;

private:
	virtual void OnSerialize(json& outJson) final;
	virtual void OnDeserialize(const json& inJson) final;
};

ME_REGISTER_COMPONENT_FOLDER(AudioSource, "Audio")