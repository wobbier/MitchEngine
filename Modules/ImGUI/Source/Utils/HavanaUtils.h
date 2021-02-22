#pragma once
#include <string>
#include "Math/Vector3.h"
#include "Math/Vector2.h"
#include <imgui.h>

namespace HavanaUtils
{
	static void Text(const std::string& Name, const Vector3& Vector)
	{
		ImGui::Text(Name.c_str());

		ImGui::Text("X: %f", Vector.x);
		ImGui::SameLine();
		ImGui::Text("Y: %f", Vector.y);
		ImGui::SameLine();
		ImGui::Text("Z: %f", Vector.z);
	}
	static void Text(const std::string& Name, const Vector2& Vector)
	{
		ImGui::Text(Name.c_str());

		ImGui::Text("X: %f", Vector.x);
		ImGui::SameLine();
		ImGui::Text("Y: %f", Vector.y);
	}
	static void EditableVector3(const std::string& Name, Vector3& Vector)
	{
		ImGui::DragFloat3(Name.c_str(), &Vector[0], 0.01f, 0.0f, 0.0f, "%.7f");
	}

	static void EditableVector(const std::string& Name, Vector2& Vector)
	{
		ImGui::DragFloat2(Name.c_str(), &Vector[0], 1.f, 0.0f, 0.0f, "%.1f");
	}
}