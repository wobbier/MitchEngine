#pragma once
#include <string>
#include "Math/Vector3.h"
#include "Math/Vector2.h"
#include <imgui.h>
#include <imgui_internal.h>

namespace HavanaUtils
{
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

	static bool EditableVector3(const std::string& Name, Vector3& Vector)
	{
		ImGui::PushID(Name.c_str());

		float columnWidth = 150.f;
		ImGui::Columns(2, "", false);
		ImGui::SetColumnWidth(0, columnWidth);
		ImGui::Text("%s", Name.c_str());
		ImGui::NextColumn();
		ImGui::PushMultiItemsWidths(3, ImGui::GetContentRegionAvailWidth() - columnWidth);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.f, 0.f });

		float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.f;
		ImVec2 buttonSize = { lineHeight + 3.f, lineHeight };

		Vector3 tempVec = Vector;
		if (ImGui::Button("X", buttonSize))
		{
			Vector.x = 0.f;
		}
		ImGui::SameLine();
		ImGui::DragFloat("##X", &Vector.x, 0.1f);
		ImGui::PopItemWidth();
		ImGui::SameLine();

		if (ImGui::Button("Y", buttonSize))
		{
			Vector.y = 0.f;
		}
		ImGui::SameLine();
		ImGui::DragFloat("##Y", &Vector.y, 0.1f);
		ImGui::PopItemWidth();
		ImGui::SameLine();

		if (ImGui::Button("Z", buttonSize))
		{
			Vector.z = 0.f;
		}
		ImGui::SameLine();
		ImGui::DragFloat("##Z", &Vector.z, 0.1f);
		ImGui::PopItemWidth();
		ImGui::SameLine();

		//ImGui::DragFloat3(Name.c_str(), &Vector[0], 0.01f, 0.0f, 0.0f, "%.7f");

		ImGui::PopStyleVar();
		ImGui::Columns(1);

		ImGui::PopID();

		return tempVec != Vector;
	}

	static void EditableVector(const std::string& Name, Vector2& Vector)
	{
		ImGui::DragFloat2(Name.c_str(), &Vector[0], 1.f, 0.0f, 0.0f, "%.1f");
	}
}
