#include "PCH.h"
#include "AudioSource.h"
#include <Audio.h>
#include "Utils/StringUtils.h"
#include "Path.h"
#include "imgui.h"
#include <filesystem>

AudioSource::AudioSource(const std::string& InFilePath)
	: Component("AudioSource")
	, FilePath(InFilePath)
{
	if (Preload)
	{

	}
}

AudioSource::AudioSource()
	: Component("AudioSource")
{

}

bool AudioSource::Load()
{
	return true;
}

void AudioSource::Play(bool ShouldLoop)
{
	if (SoundInstance)
	{
		//if (testSoundEffectInstance->GetState() != DirectX::SoundState::PLAYING)
		{
			SoundInstance->Stop(true);
		}
		SoundInstance->Play(ShouldLoop);
	}
}

void AudioSource::Stop(bool immediate)
{
	if (SoundInstance)
	{
		SoundInstance->Stop(immediate);
	}
}

void AudioSource::OnSerialize(json& outJson)
{
	outJson["FilePath"] = FilePath.LocalPath;
	outJson["PlayOnAwake"] = PlayOnAwake;
	outJson["Loop"] = Loop;
}

void AudioSource::OnDeserialize(const json& inJson)
{
	if (inJson.contains("FilePath"))
	{
		FilePath = Path(inJson["FilePath"]);
	}
	if (inJson.contains("PlayOnAwake"))
	{
		PlayOnAwake = inJson["PlayOnAwake"];
	}
	if (inJson.contains("Loop"))
	{
		Loop = inJson["Loop"];
	}
}

#if ME_EDITOR
void AudioSource::OnEditorInspect()
{

	static std::vector<Path> SoundFiles;
	Path path = Path("Assets");
	if (SoundFiles.empty())
	{
		SoundFiles.push_back(Path(""));
		for (const auto& entry : std::filesystem::recursive_directory_iterator(path.FullPath))
		{
			Path filePath(entry.path().string());
			if ((filePath.LocalPath.rfind(".wav") != std::string::npos || filePath.LocalPath.rfind(".mp3") != std::string::npos)
				&& filePath.LocalPath.rfind(".meta") == std::string::npos)
			{
				SoundFiles.push_back(filePath);
			}
		}
	}

	int i = 0;
	{
		std::string label("##SoundFile");
		if (ImGui::BeginCombo(label.c_str(), FilePath.LocalPath.c_str()))
		{
			for (int n = 0; n < SoundFiles.size(); n++)
			{
				if (ImGui::Selectable(SoundFiles[n].LocalPath.c_str(), false))
				{
					FilePath = SoundFiles[n];
					IsInitialized = false;
					if (SoundInstance && SoundInstance->GetState() == DirectX::SoundState::PLAYING)
					{
						SoundInstance->Stop(true);
					}
					SoundFiles.clear();
					break;
				}
			}
			ImGui::EndCombo();
		}

		i++;

		if (SoundInstance)
		{
			if (ImGui::Button("Play Once"))
			{
				Play();
			}
		}

		if (ImGui::Checkbox("Play On Awake", &PlayOnAwake))
		{
		}

		if (ImGui::Checkbox("Loop", &Loop))
		{
		}
	}
}
#endif

void AudioSource::Init()
{
}

