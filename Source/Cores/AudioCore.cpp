#include "PCH.h"
#include "AudioCore.h"
#include "Components/Audio/AudioSource.h"
#include "Events/AudioEvents.h"
#include "optick.h"

AudioCore::AudioCore()
	: Base(ComponentFilter().Requires<AudioSource>())
{
	SetIsSerializable(false);
//
//#if _WIN32
//	CoInitializeEx(nullptr, COINIT_MULTITHREADED);
//#endif
//	DirectX::AUDIO_ENGINE_FLAGS eflags = DirectX::AudioEngine_Default;
//#ifdef _DEBUG
//	eflags = eflags | DirectX::AudioEngine_Debug;
//#endif
//	mEngine = std::make_unique<DirectX::AudioEngine>(eflags);

	std::vector<TypeId> events = {
		PlayAudioEvent::GetEventId()
	};
	EventManager::GetInstance().RegisterReceiver(this, events);
}

void AudioCore::Update(float dt)
{
	OPTICK_CATEGORY("AudioCore Update", Optick::Category::Audio);

	//if (!mEngine->Update())
	//{
	//	// No audio device is active
	//	if (mEngine->IsCriticalError())
	//	{
	//		// RIP
	//	}
	//}
	//else
	//{
	//	auto AudioEntities = GetEntities();
	//	for (auto& ent : AudioEntities)
	//	{
	//		AudioSource& audioSource = ent.GetComponent<AudioSource>();
	//		InitComponent(audioSource);
	//	}
	//}
}

void AudioCore::InitComponent(AudioSource& audioSource)
{
	if (!audioSource.IsInitialized && !audioSource.FilePath.LocalPath.empty())
	{
		//audioSource.SoundEffectFile = std::make_unique<DirectX::SoundEffect>(mEngine.get(), StringUtils::ToWString(audioSource.FilePath.FullPath).c_str());
		//audioSource.SoundInstance = audioSource.SoundEffectFile->CreateInstance();
		audioSource.IsInitialized = true;
	}
}

void AudioCore::OnEntityAdded(Entity& NewEntity)
{
	AudioSource& comp = NewEntity.GetComponent<AudioSource>();

	InitComponent(comp);
	//// Needs to be a resource
	//comp.SoundEffectFile = std::make_unique<DirectX::SoundEffect>(mEngine.get(), StringUtils::ToWString(comp.FilePath.FullPath).c_str());
	//comp.testSoundEffectInstance = comp.SoundEffectFile->CreateInstance();
}

bool AudioCore::OnEvent(const BaseEvent& InEvent)
{
	if (InEvent.GetEventId() == PlayAudioEvent::GetEventId())
	{
		const PlayAudioEvent& evt = static_cast<const PlayAudioEvent&>(InEvent);
		Path soundPath = Path(evt.SourceName);
		if (m_cachedSounds.find(soundPath.LocalPath) == m_cachedSounds.end())
		{
			AudioSource& source = m_cachedSounds[soundPath.LocalPath] = AudioSource(soundPath.LocalPath);
			InitComponent(source);
		}
		m_cachedSounds[soundPath.LocalPath].Play(false);

		return true;
	}

	return false;
}

void AudioCore::Init()
{

}

void AudioCore::OnStart()
{
	auto AudioEntities = GetEntities();
	for (auto& ent : AudioEntities)
	{
		AudioSource& audioSource = ent.GetComponent<AudioSource>();
		if (audioSource.PlayOnAwake)
		{
			audioSource.Play(audioSource.Loop);
		}
	}
}

void AudioCore::OnStop()
{
	for (auto& audioSource : m_cachedSounds)
	{
		audioSource.second.Stop();
	}
	m_cachedSounds.clear();
}
