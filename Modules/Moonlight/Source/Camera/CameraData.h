#pragma once
#include "Math/Vector3.h"
#include "Math/Vector2.h"

namespace Moonlight { class SkyBox; }

namespace Moonlight
{
	enum class ProjectionType : uint8_t
	{
		Perspective = 0,
		Orthographic
	};

	struct CameraData
	{
		Vector3 Position;
		Vector3 Front;
		Vector3 Up;
		Vector2 OutputSize;
		ProjectionType Projection = ProjectionType::Perspective;
		float FOV = 45.0f;
		SkyBox* Skybox = nullptr;
		float OrthographicSize = 1.f;
	};
}