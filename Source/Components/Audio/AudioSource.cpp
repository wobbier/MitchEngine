#include "PCH.h"
#include "AudioSource.h"
#include "Utils/StringUtils.h"
#include "Path.h"
#include "imgui.h"
#include <filesystem>
#include <HavanaEvents.h>

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

void AudioSource::Play(bool ShouldLoop)
{
	//if (SoundInstance)
	//{
	//	//if (testSoundEffectInstance->GetState() != DirectX::SoundState::PLAYING)
	//	{
	//		SoundInstance->Stop(true);
	//	}
	//	SoundInstance->Play(ShouldLoop);
	//}
}

void AudioSource::Stop(bool immediate)
{
	//if (SoundInstance)
	//{
	//	SoundInstance->Stop(immediate);
	//}
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
	ImVec2 selectorSize(-1.f, 19.f);

	HavanaUtils::Label("Asset");
	if (!FilePath.LocalPath.empty())
	{
		selectorSize = ImVec2(ImGui::GetContentRegionAvailWidth() - 19.f, 19.f);
	}
	if (ImGui::Button(FilePath.LocalPath.empty() ? "Select Asset" : FilePath.LocalPath.c_str(), selectorSize))
	{
		RequestAssetSelectionEvent evt([this](Path selectedAsset) {
			FilePath = selectedAsset;
			IsInitialized = false;
			/*if (SoundInstance && SoundInstance->GetState() == DirectX::SoundState::PLAYING)
			{
				SoundInstance->Stop(true);
			}*/
			}, AssetType::Audio);
		evt.Fire();
	}

	if (!FilePath.LocalPath.empty())
	{
		ImGui::SameLine();
		if (ImGui::Button("X"))
		{
			FilePath = Path();
			IsInitialized = false;
		}
	}

	if (ImGui::Checkbox("Play On Awake", &PlayOnAwake))
	{
	}

	if (ImGui::Checkbox("Loop", &Loop))
	{
	}
}
#endif

void AudioSource::Init()
{
}

