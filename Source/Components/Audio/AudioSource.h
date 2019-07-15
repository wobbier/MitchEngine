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
	bool Preload = false;

	virtual void Serialize(json& outJson) override;
	virtual void Deserialize(const json& inJson) override;

#if ME_EDITOR
	virtual void OnEditorInspect() override;
#endif
	virtual void Init() override;

	bool IsInitialized = false;

	std::unique_ptr<DirectX::SoundEffect> SoundEffectFile;
	std::unique_ptr<DirectX::SoundEffectInstance> testSoundEffectInstance;

	Path FilePath;
};

ME_REGISTER_COMPONENT(AudioSource)