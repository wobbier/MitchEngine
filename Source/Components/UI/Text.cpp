#include "PCH.h"
#include "Text.h"

#if ME_EDITOR
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#endif
#include "Utils/HavanaUtils.h"

void Text::Init()
{
}

void Text::Serialize(json& outJson)
{
}

void Text::Deserialize(const json& inJson)
{
}

#if ME_EDITOR

void Text::OnEditorInspect()
{
	std::string text = SourceText;
	ImGui::InputText("Source Text", &text);
	if (text != SourceText)
	{
		SourceText = text;
		HasChanged = true;
	}
	HavanaUtils::EditableVector("Anchor Position", Anchor);
}

void Text::SetText(const std::string& NewText)
{
	SourceText = NewText;
}

#endif