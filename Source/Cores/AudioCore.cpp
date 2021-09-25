#include "PCH.h"
#include "AudioCore.h"
#include "Components/Audio/AudioSource.h"
#include "Events/AudioEvents.h"
#include "optick.h"

#ifdef FMOD_ENABLED
#include "fmod.hpp"
#endif

AudioCore::AudioCore()
	: Base(ComponentFilter().Requires<AudioSource>())
{
	SetIsSerializable(false);

#ifdef FMOD_ENABLED
	FMOD_RESULT res;
	res = FMOD::System_Create(&system);
	if (res != FMOD_OK)
	{
		YIKES("FMOD Failed to initialize: "/* + FMOD_ErrorString(res)*/);
	}
	
	res = system->init(512, FMOD_INIT_NORMAL, 0);
	if (res != FMOD_OK)
	{
		YIKES("FMOD System failed to create!");
	}
	IsInitialized = true;
#endif
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

#ifdef FMOD_ENABLED
	if (system)
	{
		system->update();
	}
#endif
	auto& AudioEntities = GetEntities();
	for (auto& ent : AudioEntities)
	{
		AudioSource& audioSource = ent.GetComponent<AudioSource>();

		InitComponent(audioSource);
	}
}

void AudioCore::InitComponent(AudioSource& audioSource)
{
	if (!audioSource.IsInitialized && !audioSource.FilePath.LocalPath.empty())
	{
#ifdef FMOD_ENABLED
		if (system->createSound(audioSource.FilePath.FullPath.c_str(), FMOD_DEFAULT, nullptr, &audioSource.SoundInstance) != FMOD_OK)
		{
			YIKES("Failed to load sound");
			audioSource.IsInitialized = true;
			return;
		}
#endif
		audioSource.IsInitialized = true;
	}
}

void AudioCore::OnEntityAdded(Entity& NewEntity)
{
	AudioSource& comp = NewEntity.GetComponent<AudioSource>();

	InitComponent(comp);
}

void AudioCore::OnEntityRemoved(Entity& NewEntity)
{
	AudioSource& comp = NewEntity.GetComponent<AudioSource>();

	comp.ClearData();
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
