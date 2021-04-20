#pragma once
#include <string>
#include "Math/Vector3.h"
#include "Math/Vector2.h"
#include <imgui.h>
#include <imgui_internal.h>

namespace HavanaUtils
{
	static void Label(const std::string& Name)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		const ImVec2 lineStart = ImGui::GetCursorScreenPos();
		const ImGuiStyle& style = ImGui::GetStyle();
		float fullWidth = ImGui::GetContentRegionAvail().x;
		float itemWidth = fullWidth * 0.6f;
		ImVec2 textSize = ImGui::CalcTextSize(Name.c_str());
		ImRect textRect;
		textRect.Min = ImGui::GetCursorScreenPos();
		textRect.Max = textRect.Min;
		textRect.Max.x += fullWidth - itemWidth;
		textRect.Max.y += textSize.y;

		ImGui::SetCursorScreenPos(textRect.Min);

		ImGui::AlignTextToFramePadding();
		textRect.Min.y += window->DC.CurrLineTextBaseOffset;
		textRect.Max.y += window->DC.CurrLineTextBaseOffset;

		ImGui::ItemSize(textRect);
		if (ImGui::ItemAdd(textRect, window->GetID(Name.c_str())))
		{
			ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), textRect.Min, textRect.Max, textRect.Max.x,
				textRect.Max.x, Name.c_str(), nullptr, &textSize);

			if (textRect.GetWidth() < textSize.x && ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", Name.c_str());
		}
		ImGui::SetCursorScreenPos({ textRect.Max.x, textSize.y + window->DC.CurrLineTextBaseOffset });
		ImGui::SameLine();
		ImGui::SetNextItemWidth(itemWidth);
	}

	static void Text(const std::string& Name, const Vector3& Vector)
	{
        ImGui::Text("%s", Name.c_str());

		ImGui::Text("X: %f", Vector.x);
		ImGui::SameLine();
		ImGui::Text("Y: %f", Vector.y);
		ImGui::SameLine();
		ImGui::Text("Z: %f", Vector.z);
	}

	static void Text(const std::string& Name, const Vector2& Vector)
	{
        ImGui::Text("%s", Name.c_str());

		ImGui::Text("X: %f", Vector.x);
		ImGui::SameLine();
		ImGui::Text("Y: %f", Vector.y);
	}

	static bool EditableVector3(const std::string& Name, Vector3& Vector, float ResetValue = 0.f)
	{
		ImGui::PushID(Name.c_str());

		float columnWidth = 150.f;
		Label(Name);

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.f, 0.f });
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, { 2.f, 2.f });

		ImGui::BeginTable("##vec", 3);
		ImGui::TableNextRow();

		float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.f;
		ImVec2 buttonSize = { lineHeight + 3.f, lineHeight };

		Vector3 tempVec = Vector;
		{
			ImGui::TableSetColumnIndex(0);
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.f, 51.f / 255.f, 82.f / 255.f, .66f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.f, 51.f / 255.f, 82.f / 255.f, 1.f));
			if (ImGui::Button("X", buttonSize))
			{
				Vector.x = ResetValue;
			}
			ImGui::PopStyleColor(2);
			ImGui::SameLine();
			ImGui::PushItemWidth(-1);
			ImGui::DragFloat("##X", &Vector.x, 0.1f);
			ImGui::PopItemWidth();
		}

		{
			ImGui::TableSetColumnIndex(1);
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(139.f / 255.f, 220.f / 255.f, 0.f, .66f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(139.f / 255.f, 220.f / 255.f, 0.f, 1.f));
			if (ImGui::Button("Y", buttonSize))
			{
				Vector.y = ResetValue;
			}
			ImGui::PopStyleColor(2);
			ImGui::SameLine();
			ImGui::PushItemWidth(-1);
			ImGui::DragFloat("##Y", &Vector.y, 0.1f);
			ImGui::PopItemWidth();
		}

		{
			ImGui::TableSetColumnIndex(2);
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(40.f / 255.f, 143.f / 255.f, 253.f / 255.f, .66f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(40.f / 255.f, 143.f / 255.f, 253.f / 255.f, 1.f));
			if (ImGui::Button("Z", buttonSize))
			{
				Vector.z = ResetValue;
			}
			ImGui::PopStyleColor(2);
			ImGui::SameLine();
			ImGui::PushItemWidth(-1);
			ImGui::DragFloat("##Z", &Vector.z, 0.1f);
			ImGui::PopItemWidth();
		}

		ImGui::EndTable();

		ImGui::PopStyleVar(2);

		ImGui::PopID();

		return tempVec != Vector;
	}

	static bool EditableVector(const std::string& Name, Vector2& Vector, float ResetValue = 0.f)
	{
		//ImGui::DragFloat2(Name.c_str(), &Vector[0], 1.f, 0.0f, 0.0f, "%.1f");
		ImGui::PushID(Name.c_str());

		float columnWidth = 150.f;
		Label(Name);

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.f, 0.f });
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, { 2.f, 2.f });

		ImGui::BeginTable("##vec", 2);
		ImGui::TableNextRow();

		float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.f;
		ImVec2 buttonSize = { lineHeight + 3.f, lineHeight };

		Vector2 tempVec = Vector;
		{
			ImGui::TableSetColumnIndex(0);
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.f, 51.f / 255.f, 82.f / 255.f, .66f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.f, 51.f / 255.f, 82.f / 255.f, 1.f));
			if (ImGui::Button("X", buttonSize))
			{
				Vector.x = ResetValue;
			}
			ImGui::PopStyleColor(2);
			ImGui::SameLine();
			ImGui::PushItemWidth(-1);
			ImGui::DragFloat("##X", &Vector.x, 0.1f);
			ImGui::PopItemWidth();
		}

		{
			ImGui::TableSetColumnIndex(1);
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(139.f / 255.f, 220.f / 255.f, 0.f, .66f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(139.f / 255.f, 220.f / 255.f, 0.f, 1.f));
			if (ImGui::Button("Y", buttonSize))
			{
				Vector.y = ResetValue;
			}
			ImGui::PopStyleColor(2);
			ImGui::SameLine();
			ImGui::PushItemWidth(-1);
			ImGui::DragFloat("##Y", &Vector.y, 0.1f);
			ImGui::PopItemWidth();
		}

		ImGui::EndTable();

		ImGui::PopStyleVar(2);

		ImGui::PopID();

		return tempVec != Vector;
	}
}
