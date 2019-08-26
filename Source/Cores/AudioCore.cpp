#include "PCH.h"
#include "AudioCore.h"
#include <Audio.h>
#include "Components/Audio/AudioSource.h"

AudioCore::AudioCore()
	: Base(ComponentFilter().Requires<AudioSource>())
{
#if _WIN32
	CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
	DirectX::AUDIO_ENGINE_FLAGS eflags = DirectX::AudioEngine_Default;
#ifdef _DEBUG
	eflags = eflags | DirectX::AudioEngine_Debug;
#endif
	mEngine = std::make_unique<DirectX::AudioEngine>(eflags);
}

void AudioCore::Update(float dt)
{
	if (!mEngine->Update())
	{
		// No audio device is active
		if (mEngine->IsCriticalError())
		{
			// RIP
		}
	}
	else
	{
		auto AudioEntities = GetEntities();
		for (auto& ent : AudioEntities)
		{
			auto& audioSource = ent.GetComponent<AudioSource>();
			if (!audioSource.IsInitialized && !audioSource.FilePath.LocalPath.empty())
			{
				audioSource.SoundEffectFile = std::make_unique<DirectX::SoundEffect>(mEngine.get(), StringUtils::ToWString(audioSource.FilePath.FullPath).c_str());
				audioSource.testSoundEffectInstance = audioSource.SoundEffectFile->CreateInstance();
				audioSource.IsInitialized = true;
			}
		}
	}
}

void AudioCore::OnEntityAdded(Entity& NewEntity)
{
	auto& comp = NewEntity.GetComponent<AudioSource>();

	// Needs to be a resource
	comp.SoundEffectFile = std::make_unique<DirectX::SoundEffect>(mEngine.get(), StringUtils::ToWString(comp.FilePath.FullPath).c_str());
	comp.testSoundEffectInstance = comp.SoundEffectFile->CreateInstance();
}

void AudioCore::Init()
{

}
