#include "PCH.h"
#include <filesystem>

#include "BasicUIView.h"
#include "imgui.h"
#include "UI/JSHelpers.h"
#include "Ultralight/View.h"
#include "Events/AudioEvents.h"
#include "Ultralight/String.h"

BasicUIView::BasicUIView()
	: Component("BasicUIView")
{

}

BasicUIView::BasicUIView(const char* Name)
	: Component(Name)
{

}

void BasicUIView::Init()
{
	SourceFile = File(FilePath);
}

void BasicUIView::Serialize(json& outJson)
{
	Component::Serialize(outJson);

	outJson["FilePath"] = FilePath.LocalPath;
}

void BasicUIView::Deserialize(const json& inJson)
{
	Component::Deserialize(inJson);
	if (inJson.contains("FilePath"))
	{
		FilePath = Path(inJson["FilePath"]);
	}
}

void BasicUIView::OnBeginLoading(ultralight::View* caller)
{
}

void BasicUIView::OnFinishLoading(ultralight::View* caller)
{
}

void BasicUIView::OnUpdateHistory(ultralight::View* caller)
{
}

void BasicUIView::OnDOMReady(ultralight::View* caller)
{
	ultralight::SetJSContext(caller->js_context());
	ultralight::JSObject global = ultralight::JSGlobalObject();

	global["PlaySound"] = BindJSCallback(&BasicUIView::PlaySound);

	OnUILoad(global, caller);
}

void BasicUIView::OnUILoad(ultralight::JSObject& GlobalWindow, ultralight::View* Caller)
{
}


void BasicUIView::PlaySound(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args)
{
	if (args.empty() || !args[0].IsString())
	{
		BRUH("PlaySound(string) argument mismatch.");
		return;
	}
	ultralight::String str = args[0].ToString();
	std::string str2(str.utf8().data());
	PlayAudioEvent evt(str2);
	evt.Fire();
}

#if ME_EDITOR

void BasicUIView::OnEditorInspect()
{
	static std::vector<Path> Textures;
	if (Textures.empty())
	{
		Path path = Path("Assets");
		Textures.push_back(Path(""));
		for (const auto& entry : std::filesystem::recursive_directory_iterator(path.FullPath))
		{
			Path filePath(entry.path().string());
			if (filePath.LocalPath.rfind(".html") != std::string::npos && filePath.LocalPath.rfind(".meta") == std::string::npos)
			{
				Textures.push_back(filePath);
			}
		}
	}

	if (ImGui::BeginCombo("##HTMLSource", FilePath.LocalPath.c_str()))
	{
		for (int n = 0; n < Textures.size(); n++)
		{
			if (ImGui::Selectable(Textures[n].LocalPath.c_str(), false))
			{
				FilePath = Textures[n];
				SourceFile = File(FilePath);
				IsInitialized = false;
				Textures.clear();
				break;
			}
		}
		ImGui::EndCombo();
	}
}

#endif