#include "PCH.h"
#include "AudioSource.h"
#include "Utils/StringUtils.h"
#include "Path.h"
#include "imgui.h"
#include "Events/HavanaEvents.h"
#include <Editor/AssetDescriptor.h>

#include "Events/AudioEvents.h"

#if USING( ME_FMOD )
#include "fmod.hpp"
#endif
#include "Resource/MetaFile.h"

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
#if USING( ME_FMOD )
	if (SoundInstance)
	{
		if (IsPlaying())
		{
			Stop(true);
		}
		SoundInstance->Handle->getSystemObject(&m_owner);
		m_owner->playSound(SoundInstance->Handle, nullptr, false, &ChannelHandle);
	}
#endif
}

void AudioSource::Stop(bool immediate)
{
#if USING( ME_FMOD )
	if (ChannelHandle)
	{
		ChannelHandle->stop();
	}
#endif
}

bool AudioSource::IsPlaying() const
{
#if USING( ME_FMOD )
	bool isPlaying;
	return (ChannelHandle && ChannelHandle->isPlaying(&isPlaying) == FMOD_OK && isPlaying);
#else
	return false;
#endif
}

unsigned int AudioSource::GetLength()
{
#if USING( ME_FMOD )
	unsigned int isPlaying = 0;
	if (SoundInstance && SoundInstance->Handle && SoundInstance->Handle->getLength(&isPlaying, FMOD_TIMEUNIT_MS) != FMOD_OK)
	{
	}
	return isPlaying;
#else
	return 0.f;
#endif
}

unsigned int AudioSource::GetPositionMs()
{
#if USING( ME_FMOD )
	unsigned int isPlaying = 0;
	if (ChannelHandle && ChannelHandle->getPosition(&isPlaying, FMOD_TIMEUNIT_MS) != FMOD_OK)
	{
	}
	return isPlaying;
#else
	return 0.f;
#endif
}

void AudioSource::SetPositionMs(unsigned int position)
{
#if USING( ME_FMOD )
	if (ChannelHandle && ChannelHandle->setPosition(position, FMOD_TIMEUNIT_MS) != FMOD_OK)
	{
	}
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

#if USING( ME_EDITOR )
void AudioSource::OnEditorInspect()
{
#if !USING( ME_FMOD )
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
#if USING( ME_FMOD )
	SoundInstance = nullptr;
	m_owner = nullptr;
#endif

	IsInitialized = false;
	FilePath = Path();
}

std::string AudioResourceMetadata::GetExtension2() const
{
	return "wav";
}

void AudioResourceMetadata::OnSerialize(json& inJson)
{
}

void AudioResourceMetadata::OnDeserialize(const json& inJson)
{
}

#if USING( ME_EDITOR )

void AudioResourceMetadata::Export()
{
	MetaBase::Export();
}

void AudioResourceMetadata::OnEditorInspect()
{
	MetaBase::OnEditorInspect();
#if !USING( ME_FMOD )
	ImGui::Separator();
	ImGui::Text("FMOD NOT ENABLED! See Help > About.");
	return;
#endif
	static SharedPtr<AudioSource> source = nullptr;
	if(source && source->FilePath.LocalPath != FilePath.LocalPath)
	{
		source->Stop();
		source = nullptr;
	}

	if (source && source->IsPlaying())
	{
		if (ImGui::Button("Stop"))
		{
			source->Stop();
		}
	}
	else if (ImGui::Button("Play"))
	{
		if (!source || (source && !source->IsInitialized))
		{
			PlayAudioEvent evt;
			evt.SourceName = FilePath.FullPath;
			evt.Callback = [](SharedPtr<AudioSource> loadedAudio) { source = loadedAudio; };
			evt.Fire();
		}
		else
		{
			source->Play();
		}
	}
	ImGui::SameLine();
	float progress = 0.f;
	if(source && source->GetLength() > 0)
	{
		progress = (float)source->GetPositionMs() / (float)source->GetLength();
	}
	ImGui::ProgressBar(progress);
	if(ImGui::Button("Seek Half WAy") && source)
	{
		float half = (float)source->GetLength() / 2.f;

		source->SetPositionMs(half);
	}

	/*if (ImGui::IsItemActive())
	{
		if (!source)
		{
			source->Stop();
		}
		
	}*/
	if(!source)
	{
		return;
	}

}

#endif

std::string AudioResourceMetadataMp3::GetExtension2() const
{
	return "mp3";
}
