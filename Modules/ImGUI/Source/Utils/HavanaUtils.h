#pragma once
#include <string>
#include "Math/Vector3.h"
#include "Math/Vector2.h"

class HavanaUtils
{
	HavanaUtils() = delete;

public:
	static float Label(const std::string& Name);

	static void Text(const std::string& Name, const Vector3& Vector);
	static void Text(const std::string& Name, const Vector2& Vector);

	static bool EditableVector3(const std::string& Name, Vector3& Vector, float ResetValue = 0.f);
	static bool EditableVector(const std::string& Name, Vector2& Vector, float ResetValue = 0.f);

	static void ColorButton(const std::string& Name, Vector3& arr);
};