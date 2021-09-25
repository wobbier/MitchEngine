#include "PCH.h"
#include "AudioSource.h"
#include "Utils/StringUtils.h"
#include "Path.h"
#include "imgui.h"
#include "HavanaEvents.h"
#include <Editor/AssetDescriptor.h>

#ifdef FMOD_ENABLED
#include "fmod.hpp"
#endif

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
#ifdef FMOD_ENABLED
	if (SoundInstance)
	{
		if (IsPlaying())
		{
			Stop(true);
		}
		SoundInstance->getSystemObject(&m_owner);
		m_owner->playSound(SoundInstance, nullptr, false, &ChannelHandle);
	}
#endif
}

void AudioSource::Stop(bool immediate)
{
#ifdef FMOD_ENABLED
	if (ChannelHandle)
	{
		ChannelHandle->stop();
	}
#endif
}

bool AudioSource::IsPlaying() const
{
#ifdef FMOD_ENABLED
	FMOD_OPENSTATE mode;
	bool isPlaying;
	return (ChannelHandle && ChannelHandle->isPlaying(&isPlaying) == FMOD_OK && isPlaying);
#else
	return false;
#endif
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
#ifndef FMOD_ENABLED
	ImGui::Text("FMOD NOT ENABLED! See Help > About.");
	return;
#endif
	ImVec2 selectorSize(-1.f, 19.f);

	HavanaUtils::Label("Asset");
	if (!FilePath.LocalPath.empty())
	{
		selectorSize = ImVec2(ImGui::GetContentRegionAvailWidth() - 19.f, 19.f);
	}
	if (ImGui::Button(FilePath.LocalPath.empty() ? "Select Asset" : FilePath.LocalPath.c_str(), selectorSize))
	{
		RequestAssetSelectionEvent evt([this](Path selectedAsset) {
			ClearData();
			FilePath = selectedAsset;
			}, AssetType::Audio);
		evt.Fire();
	}
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetDescriptor::kDragAndDropPayload))
		{
			IM_ASSERT(payload->DataSize == sizeof(AssetDescriptor));
			AssetDescriptor& payload_n = *(AssetDescriptor*)payload->Data;

			if (payload_n.Type == AssetType::Audio)
			{
				ClearData();
				FilePath = payload_n.FullPath;
			}
		}
		ImGui::EndDragDropTarget();
	}

	if (!FilePath.LocalPath.empty())
	{
		ImGui::SameLine();
		if (ImGui::Button("X"))
		{
			ClearData();
		}
	}

	if (ImGui::Checkbox("Play On Awake", &PlayOnAwake))
	{
	}

	if (ImGui::Checkbox("Loop", &Loop))
	{
	}

	if (IsPlaying())
	{
		if (ImGui::Button("Stop"))
		{
			Stop();
		}
	}
	else if (ImGui::Button("Play"))
	{
		Play(false);
	}
}
#endif

void AudioSource::Init()
{
}

void AudioSource::ClearData()
{
#ifdef FMOD_ENABLED
	if (SoundInstance)
	{
		SoundInstance->release();
	}
	SoundInstance = nullptr;
	m_owner = nullptr;
#endif

	IsInitialized = false;
	FilePath = Path();
}

